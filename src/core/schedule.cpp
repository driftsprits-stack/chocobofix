#include "core/schedule.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace ta {

void ComputeOccupancy(const Instance& inst, Plan* plan) {
  plan->slots.clear();
  plan->slots_used.clear();

  // location-week -> activities present, deterministic order
  std::map<std::pair<LocIdx, Week>, std::vector<ActIdx>> present;
  for (const auto& acc : plan->accesses)
    for (LocIdx l : inst.activities[acc.activity].occupied)
      present[{l, acc.week}].push_back(acc.activity);

  for (auto& [key, acts] : present) {
    std::sort(acts.begin(), acts.end(), [&](ActIdx a, ActIdx b) {
      return inst.activities[a].id < inst.activities[b].id;
    });
    acts.erase(std::unique(acts.begin(), acts.end()), acts.end());

    std::vector<ActIdx> pm, pc, co;
    for (ActIdx a : acts) {
      switch (inst.ContractOf(inst.activities[a]).access_type) {
        case AccessType::kPM: pm.push_back(a); break;
        case AccessType::kPC: pc.push_back(a); break;
        default: co.push_back(a); break;
      }
    }
    auto& out = plan->slots[key];
    int slot = 0;
    for (ActIdx a : pm) out[a] = ++slot;             // a PM is alone in its slot
    size_t ci = 0;
    for (ActIdx a : pc) {                            // one PC hosts up to three C
      ++slot;
      out[a] = slot;
      for (int k = 0; k < 3 && ci < co.size(); ++k, ++ci) out[co[ci]] = slot;
    }
    while (ci < co.size()) {                         // remaining C pack four to a slot
      ++slot;
      for (int k = 0; k < 4 && ci < co.size(); ++k, ++ci) out[co[ci]] = slot;
    }
    plan->slots_used[key] = slot;
  }
}

bool AssignAccessNights(const Instance& inst, Plan* plan, std::string* error) {
  // (contract, access_type, week) -> activities taking an access that week
  std::map<std::tuple<ConIdx, int, Week>, std::vector<size_t>> bucket;
  for (size_t i = 0; i < plan->accesses.size(); ++i) {
    const auto& a = inst.activities[plan->accesses[i].activity];
    bucket[{a.contract, static_cast<int>(inst.ContractOf(a).access_type),
            plan->accesses[i].week}].push_back(i);
  }
  for (auto& [key, idxs] : bucket) {
    const auto& c = inst.contracts[std::get<0>(key)];
    const int capacity = c.max_access_per_week * c.number_of_workfronts;
    if (static_cast<int>(idxs.size()) > capacity) {
      std::ostringstream os;
      os << "contract " << c.number << " week " << std::get<2>(key) << ": "
         << idxs.size() << " activities exceed " << c.max_access_per_week
         << " nights x " << c.number_of_workfronts << " workfronts";
      *error = os.str();
      return false;
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t x, size_t y) {
      return inst.activities[plan->accesses[x].activity].id <
             inst.activities[plan->accesses[y].activity].id;
    });
    // Fill night 1 to its workfront limit, then night 2, and so on. This keeps
    // the number of distinct nights used at the minimum the week requires.
    for (size_t k = 0; k < idxs.size(); ++k)
      plan->accesses[idxs[k]].access_night =
          static_cast<int>(k / c.number_of_workfronts) + 1;
  }
  return true;
}

std::vector<Week> ActivityLastWeek(const Instance& inst, const Plan& plan) {
  std::vector<Week> last(inst.activities.size(), 0);
  for (const auto& a : plan.accesses) last[a.activity] = std::max(last[a.activity], a.week);
  return last;
}

std::vector<Week> ContractLastWeek(const Instance& inst, const Plan& plan) {
  std::vector<Week> last(inst.contracts.size(), 0);
  for (const auto& a : plan.accesses) {
    const ConIdx c = inst.activities[a.activity].contract;
    last[c] = std::max(last[c], a.week);
  }
  return last;
}

Score ComputeScore(const Instance& inst, const Plan& plan) {
  Score s;
  s.nights_scheduled = static_cast<int>(plan.accesses.size());
  for (const auto& a : plan.accesses) if (a.eclo) ++s.eclo_nights_total;

  const auto clast = ContractLastWeek(inst, plan);
  for (size_t c = 0; c < inst.contracts.size(); ++c) {
    if (clast[c] == 0) continue;
    const int days = static_cast<int>(inst.SundayOfWeek(clast[c]) - inst.contracts[c].planned_completion_date);
    if (days > 0) { s.overrun_days_total += days; ++s.contracts_overrunning; }
    else s.earliness_days_total += -days;
  }
  // R7: the weighted term accumulates per activity, against its contract's date.
  const auto alast = ActivityLastWeek(inst, plan);
  for (size_t i = 0; i < inst.activities.size(); ++i) {
    if (alast[i] == 0) continue;
    const auto& c = inst.ContractOf(inst.activities[i]);
    const int days = static_cast<int>(inst.SundayOfWeek(alast[i]) - c.planned_completion_date);
    if (days <= 0) continue;
    s.priority_overrun[c.priority] += days;
    s.priority_weighted_tenths += static_cast<long long>(ContractWeight(c.priority)) *
                                  ActivityNudgeTenths(inst.activities[i].activity_priority) * days;
  }
  for (const auto& [key, used] : plan.slots_used)
    s.excess_access_nights_total +=
        std::max(0, used - EffectiveSupply(inst, plan, key.first, key.second));

  switch (plan.scenario) {
    case Scenario::kA:
      s.objective_tenths = s.priority_weighted_tenths;
      break;
    case Scenario::kB:
      s.objective_tenths = 10LL * (kExcessAccessPenalty * s.excess_access_nights_total +
                                   kEcloPenalty * s.eclo_nights_total);
      break;
    case Scenario::kC:
      s.objective_tenths = s.priority_weighted_tenths +
                           10LL * (kExcessAccessPenalty * s.excess_access_nights_total +
                                   kEcloPenalty * s.eclo_nights_total);
      break;
  }
  return s;
}

bool ExportPlan(const Instance& inst, const Plan& plan, const std::string& dir, std::string* error) {
  std::vector<Access> ordered = plan.accesses;
  std::sort(ordered.begin(), ordered.end(), [&](const Access& x, const Access& y) {
    const auto& ax = inst.activities[x.activity].id;
    const auto& ay = inst.activities[y.activity].id;
    return ax != ay ? ax < ay : x.week < y.week;
  });

  {
    CsvWriter w(dir + "/SCHEDULE_ACCESS.csv",
                {"activity_id", "access_seq", "week", "eclo", "access_night"});
    std::string prev;
    int seq = 0;
    for (const auto& a : ordered) {
      const auto& id = inst.activities[a.activity].id;
      seq = (id == prev) ? seq + 1 : 1;
      prev = id;
      w.Row({id, std::to_string(seq), std::to_string(a.week),
             a.eclo ? "1" : "0", std::to_string(a.access_night)});
    }
    if (!w.Commit(error)) return false;
  }
  {
    CsvWriter w(dir + "/SCHEDULE_OCCUPANCY.csv",
                {"activity_id", "week", "location_id", "co_share_group"});
    // Emit grouped by activity then week then location, matching the sample's shape.
    std::map<std::pair<std::string, Week>, std::vector<std::pair<std::string, int>>> rows;
    for (const auto& [key, m] : plan.slots)
      for (const auto& [act, slot] : m)
        rows[{inst.activities[act].id, key.second}].push_back({inst.locations[key.first].id, slot});
    for (auto& [key, v] : rows) {
      std::sort(v.begin(), v.end());
      for (const auto& [loc, slot] : v)
        w.Row({key.first, std::to_string(key.second), loc, "b" + std::to_string(slot)});
    }
    if (!w.Commit(error)) return false;
  }
  {
    CsvWriter w(dir + "/RESULTS.csv",
                {"scenario", "contract_number", "simulated_completion_date", "overrun_days"});
    const auto clast = ContractLastWeek(inst, plan);
    for (size_t c = 0; c < inst.contracts.size(); ++c) {
      if (clast[c] == 0) continue;   // no access scheduled: omitted rather than faked
      const Date sim = inst.SundayOfWeek(clast[c]);
      const int overrun = std::max(0, static_cast<int>(sim - inst.contracts[c].planned_completion_date));
      w.Row({std::string(ToString(plan.scenario)), inst.contracts[c].number,
             sim.ToIso(), std::to_string(overrun)});
    }
    if (!w.Commit(error)) return false;
  }
  // Provenance travels with the plan. A repaired plan checked against nominal
  // supply is being checked against a question it was not asked.
  {
    std::ostringstream os;
    os << "{\n  \"strict_buffers\": " << (plan.provenance.strict_buffers ? "true" : "false")
       << ",\n  \"fallback\": " << (plan.provenance.fallback ? "true" : "false")
       << ",\n  \"reproducible\": " << (plan.provenance.deterministic ? "true" : "false")
       << ",\n  \"search_workers\": " << plan.provenance.workers
       << ",\n  \"random_seed\": " << plan.provenance.random_seed
       << ",\n  \"supply_overrides\": [";
    for (size_t i = 0; i < plan.provenance.supply_overrides.size(); ++i) {
      const auto& [loc, wk, sup] = plan.provenance.supply_overrides[i];
      os << (i ? ",\n    " : "\n    ") << "{\"location_id\": \"" << loc
         << "\", \"week\": " << wk << ", \"supply\": " << sup << "}";
    }
    os << (plan.provenance.supply_overrides.empty() ? "" : "\n  ") << "]\n}\n";
    const std::string body = os.str();
    const std::string path = dir + "/PROVENANCE.json";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { *error = "cannot write " + path; return false; }
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
  }
  return true;
}

int EffectiveSupply(const Instance& inst, const Plan& plan, LocIdx loc, Week week) {
  for (const auto& [id, w, sup] : plan.provenance.supply_overrides)
    if (w == week && inst.locations[loc].id == id) return sup;
  return inst.locations[loc].supply_capacity;
}

bool LoadPlan(const Instance& inst, const std::string& dir, Plan* out,
              std::vector<InputError>* errors) {
  CsvTable access, occ, results;
  if (!CsvTable::Load(dir + "/SCHEDULE_ACCESS.csv",
                      {"activity_id", "access_seq", "week", "eclo", "access_night"}, &access, errors))
    return false;
  if (!CsvTable::Load(dir + "/SCHEDULE_OCCUPANCY.csv",
                      {"activity_id", "week", "location_id", "co_share_group"}, &occ, errors))
    return false;
  if (!CsvTable::Load(dir + "/RESULTS.csv",
                      {"scenario", "contract_number", "simulated_completion_date", "overrun_days"},
                      &results, errors))
    return false;

  std::string scen;
  for (int r = 0; r < results.RowCount(); ++r) {
    const std::string s = results.Str(r, "scenario", errors);
    if (scen.empty()) scen = s;
    else if (s != scen)
      results.AddError(r + 2, "scenario",
                       "RESULTS.csv mixes scenarios (\"" + scen + "\" and \"" + s + "\")", errors);
  }
  const auto sc = ParseScenario(scen);
  if (!sc) { results.AddError(2, "scenario", "expected A, B or C", errors); return false; }
  out->scenario = *sc;

  // Keep the RESULTS rows as written; the validator recomputes and compares.
  for (int r = 0; r < results.RowCount(); ++r) {
    ResultRow row;
    row.scenario = results.Str(r, "scenario", errors);
    row.contract_number = results.Str(r, "contract_number", errors);
    row.simulated_completion_date = results.DateOf(r, "simulated_completion_date", errors);
    row.overrun_days = results.Int(r, "overrun_days", errors, 0, 100000);
    out->results_rows.push_back(std::move(row));
  }

  // Provenance, when the producer wrote it. Absent means nominal supply.
  {
    std::string body;
    if (ReadFile(dir + "/PROVENANCE.json", &body)) {
      out->provenance.strict_buffers = body.find("\"strict_buffers\": true") != std::string::npos;
      out->provenance.fallback = body.find("\"fallback\": true") != std::string::npos;
      size_t pos = 0;
      while ((pos = body.find("\"location_id\"", pos)) != std::string::npos) {
        const auto q1 = body.find('"', body.find(':', pos) + 1);
        const auto q2 = body.find('"', q1 + 1);
        const auto wk = body.find("\"week\"", q2);
        const auto sp = body.find("\"supply\"", q2);
        if (q1 == std::string::npos || q2 == std::string::npos ||
            wk == std::string::npos || sp == std::string::npos) break;
        try {
          out->provenance.supply_overrides.emplace_back(
              body.substr(q1 + 1, q2 - q1 - 1),
              std::stoi(body.substr(body.find(':', wk) + 1, 12)),
              std::stoi(body.substr(body.find(':', sp) + 1, 12)));
        } catch (...) {}
        pos = q2 + 1;
      }
    }
  }

  for (int r = 0; r < access.RowCount(); ++r) {
    Access a;
    const std::string id = access.Str(r, "activity_id", errors);
    auto it = inst.activity_by_id.find(id);
    if (it == inst.activity_by_id.end()) {
      access.AddError(r + 2, "activity_id", "\"" + id + "\" is not in this instance", errors);
      continue;
    }
    a.activity = it->second;
    a.week = access.Int(r, "week", errors, 1, inst.horizon_weeks);
    a.eclo = access.Int(r, "eclo", errors, 0, 1) == 1;
    a.access_night = access.Int(r, "access_night", errors, 1, 7);
    a.access_seq = access.Int(r, "access_seq", errors, 1, 100000);
    out->accesses.push_back(a);
  }

  // Occupancy is read back as authored rather than recomputed, so the validator
  // judges the emitted file and not what the exporter believes it emitted.
  for (int r = 0; r < occ.RowCount(); ++r) {
    const std::string id = occ.Str(r, "activity_id", errors);
    const std::string loc = occ.Str(r, "location_id", errors);
    auto ait = inst.activity_by_id.find(id);
    auto lit = inst.location_by_id.find(loc);
    if (ait == inst.activity_by_id.end()) {
      occ.AddError(r + 2, "activity_id", "\"" + id + "\" is not in this instance", errors); continue;
    }
    if (lit == inst.location_by_id.end()) {
      occ.AddError(r + 2, "location_id", "\"" + loc + "\" is not in 04_LOCATION_SUPPLY.csv", errors); continue;
    }
    const Week w = occ.Int(r, "week", errors, 1, inst.horizon_weeks);
    const std::string g = occ.Str(r, "co_share_group", errors);
    int slot = 0;
    if (g.size() >= 2 && g[0] == 'b') { try { slot = std::stoi(g.substr(1)); } catch (...) { slot = 0; } }
    if (slot <= 0) {
      occ.AddError(r + 2, "co_share_group", "expected a label of the form b1, b2, ...", errors); continue;
    }
    out->slots[{lit->second, w}][ait->second] = slot;
  }
  for (const auto& [key, m] : out->slots) {
    int mx = 0;
    for (const auto& [a, s] : m) mx = std::max(mx, s);
    // Slots actually distinct at this location-week, which is what capacity limits.
    std::vector<int> distinct;
    for (const auto& [a, s] : m) distinct.push_back(s);
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    out->slots_used[key] = static_cast<int>(distinct.size());
    (void)mx;
  }
  return errors->empty();
}

}  // namespace ta
