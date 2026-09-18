#include "validator/validator.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace ta {
namespace {

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o.push_back(c);
    }
  }
  return o;
}

}  // namespace

ValidationReport Validate(const Instance& inst, const Plan& plan, ValidationOptions opts) {
  ValidationReport rep;
  rep.scenario = plan.scenario;
  rep.strict_buffers = opts.strict_buffers || plan.provenance.strict_buffers;
  rep.supply_overrides = static_cast<int>(plan.provenance.supply_overrides.size());
  rep.fallback = plan.provenance.fallback;
  auto fail = [&](const char* rule, std::string detail) {
    rep.hard_violations.push_back({rule, "hard", std::move(detail)});
  };

  // ---- one access per activity per week (R4) ------------------------------
  std::map<std::pair<ActIdx, Week>, int> per_act_week;
  for (const auto& a : plan.accesses) ++per_act_week[{a.activity, a.week}];
  for (const auto& [k, n] : per_act_week)
    if (n > 1)
      fail("allocation", "wk" + std::to_string(k.second) + ": " + inst.activities[k.first].id +
                             " holds " + std::to_string(n) + " accesses in one week (max 1)");

  // ---- workload conservation (rule 1) -------------------------------------
  // Exact integer arithmetic in tenths: a standard night yields 10, ECLO 15.
  std::vector<int> yield(inst.activities.size(), 0);
  for (const auto& a : plan.accesses)
    yield[a.activity] += a.eclo ? kEcloYieldTenths : kStdYieldTenths;
  for (size_t i = 0; i < inst.activities.size(); ++i) {
    const int need = inst.activities[i].total_accesses * kStdYieldTenths;
    if (yield[i] < need) {
      std::ostringstream os;
      os << inst.activities[i].id << ": scheduled " << (yield[i] / 10.0)
         << " of " << inst.activities[i].total_accesses << " required access units";
      fail("workload", os.str());
    }
  }

  // ---- planned start week (rule 2) ----------------------------------------
  for (const auto& a : plan.accesses)
    if (a.week < inst.activities[a.activity].earliest_week)
      fail("planned_start", "wk" + std::to_string(a.week) + ": " + inst.activities[a.activity].id +
                                " starts before its planned start week " +
                                std::to_string(inst.activities[a.activity].earliest_week));

  // ---- predecessor precedence, FS+0 strictly later week (rule 3) ----------
  const auto alast = ActivityLastWeek(inst, plan);
  std::vector<Week> afirst(inst.activities.size(), 0);
  for (const auto& a : plan.accesses)
    afirst[a.activity] = afirst[a.activity] ? std::min(afirst[a.activity], a.week) : a.week;
  for (size_t i = 0; i < inst.activities.size(); ++i) {
    const ActIdx p = inst.activities[i].predecessor;
    if (p == kNoIndex || afirst[i] == 0 || alast[p] == 0) continue;
    if (afirst[i] <= alast[p])
      fail("precedence", inst.activities[i].id + " first accesses in wk" + std::to_string(afirst[i]) +
                             ", not strictly after predecessor " + inst.activities[p].id +
                             " which finishes in wk" + std::to_string(alast[p]));
  }

  // ---- occupancy completeness --------------------------------------------
  // Every week an activity takes an access it must book exactly its expanded
  // span - no more, no fewer. Catches a truncated or padded occupancy file.
  std::map<std::pair<ActIdx, Week>, std::set<LocIdx>> booked;
  for (const auto& [key, m] : plan.slots)
    for (const auto& [act, slot] : m) booked[{act, key.second}].insert(key.first);
  for (const auto& [k, n] : per_act_week) {
    std::set<LocIdx> want(inst.activities[k.first].occupied.begin(),
                          inst.activities[k.first].occupied.end());
    const auto& got = booked[k];
    if (got != want) {
      std::ostringstream os;
      os << "wk" << k.second << ": " << inst.activities[k.first].id << " books "
         << got.size() << " locations, expected " << want.size() << " for span "
         << inst.activities[k.first].start_location_id << " -> "
         << inst.activities[k.first].end_location_id;
      fail("occupancy", os.str());
    }
  }
  for (const auto& [k, locs] : booked)
    if (!per_act_week.count(k))
      fail("occupancy", "wk" + std::to_string(k.second) + ": " + inst.activities[k.first].id +
                            " occupies locations without a matching access row");

  // ---- access_seq is part of the schema, so it is checked ------------------
  // Per activity, the sequence must run 1..n with no gaps or repeats, and must
  // ascend with the week. An exporter that gets this wrong produces a file that
  // parses but misrepresents the order of the work.
  {
    std::map<ActIdx, std::vector<std::pair<Week, int>>> seq;
    for (const auto& a : plan.accesses) seq[a.activity].push_back({a.week, a.access_seq});
    for (auto& [act, v] : seq) {
      std::sort(v.begin(), v.end());
      std::set<int> seen;
      int prev_seq = 0;
      for (size_t k = 0; k < v.size(); ++k) {
        const int want = static_cast<int>(k) + 1;
        if (v[k].second != want) {
          std::ostringstream os;
          os << inst.activities[act].id << ": access_seq " << v[k].second << " in wk"
             << v[k].first << " should be " << want
             << " (the sequence must run 1.." << v.size() << " in week order)";
          fail("schema", os.str());
        }
        if (!seen.insert(v[k].second).second)
          fail("schema", inst.activities[act].id + ": access_seq " +
                             std::to_string(v[k].second) + " appears more than once");
        if (v[k].second <= prev_seq && k > 0)
          fail("schema", inst.activities[act].id + ": access_seq does not ascend with the week");
        prev_seq = v[k].second;
      }
    }
  }

  // ---- RESULTS.csv is recomputed and compared, not trusted -----------------
  {
    const auto clast = ContractLastWeek(inst, plan);
    std::map<std::string, const ResultRow*> rows;
    for (const auto& r : plan.results_rows) {
      if (r.scenario != std::string(ToString(plan.scenario)))
        fail("schema", "RESULTS.csv row for " + r.contract_number + " says scenario \"" +
                           r.scenario + "\" but the file is scenario " +
                           std::string(ToString(plan.scenario)));
      if (!rows.emplace(r.contract_number, &r).second)
        fail("schema", "RESULTS.csv lists contract " + r.contract_number + " more than once");
      if (!inst.contract_by_number.count(r.contract_number))
        fail("schema", "RESULTS.csv lists contract " + r.contract_number +
                           ", which is not in this instance");
    }
    for (size_t c = 0; c < inst.contracts.size(); ++c) {
      const auto& con = inst.contracts[c];
      auto it = rows.find(con.number);
      if (clast[c] == 0) {
        if (it != rows.end())
          fail("schema", "RESULTS.csv reports a completion for " + con.number +
                             ", which has no scheduled access");
        continue;
      }
      if (it == rows.end()) {
        fail("schema", "RESULTS.csv omits " + con.number + ", which has scheduled work");
        continue;
      }
      const Date want_date = inst.SundayOfWeek(clast[c]);
      const int want_over = std::max(0, static_cast<int>(want_date - con.planned_completion_date));
      if (!(it->second->simulated_completion_date == want_date))
        fail("schema", "RESULTS.csv gives " + con.number + " a completion of " +
                           it->second->simulated_completion_date.ToIso() +
                           "; the schedule says " + want_date.ToIso());
      if (it->second->overrun_days != want_over)
        fail("schema", "RESULTS.csv gives " + con.number + " an overrun of " +
                           std::to_string(it->second->overrun_days) + " days; recomputed " +
                           std::to_string(want_over));
    }
  }

  // ---- capacity (rule 5 / R2) --------------------------------------------
  // Scenario A: zero tolerance. C: at most one excess access-night per
  // location-week. B: excess is scored, never hard-failed.
  const int tolerance = plan.scenario == Scenario::kA ? 0
                      : plan.scenario == Scenario::kC ? 1 : 1 << 30;
  for (const auto& [key, used] : plan.slots_used) {
    const auto& L = inst.locations[key.first];
    // Honour a repair's supply override: a disrupted plan must be checked
    // against the supply it was actually produced for.
    const int supply = EffectiveSupply(inst, plan, key.first, key.second);
    const int excess = used - supply;
    if (excess > 0) {
      std::ostringstream os;
      os << "wk" << key.second << ": " << L.id << " uses " << used
         << " access-nights against a supply of " << supply
         << (supply == L.supply_capacity ? "" : " (reduced by a recorded disruption)");
      rep.capacity_hotspots.push_back(os.str());
      if (excess > tolerance) fail("capacity", os.str());
    } else if (used == supply) {
      rep.capacity_hotspots.push_back("wk" + std::to_string(key.second) + ": " + L.id + " at capacity");
    }
  }

  // ---- legal mix within a slot (rule 5) -----------------------------------
  std::map<std::tuple<LocIdx, Week, int>, std::vector<ActIdx>> by_slot;
  for (const auto& [key, m] : plan.slots)
    for (const auto& [act, slot] : m) by_slot[{key.first, key.second, slot}].push_back(act);
  for (const auto& [key, acts] : by_slot) {
    int pm = 0, pc = 0, co = 0;
    for (ActIdx a : acts) switch (inst.ContractOf(inst.activities[a]).access_type) {
      case AccessType::kPM: ++pm; break;
      case AccessType::kPC: ++pc; break;
      default: ++co; break;
    }
    const bool ok = (pm == 1 && pc == 0 && co == 0) ||
                    (pm == 0 && pc <= 1 && pc + co <= 4);
    if (!ok) {
      std::ostringstream os;
      os << "wk" << std::get<1>(key) << ": " << inst.locations[std::get<0>(key)].id
         << " slot b" << std::get<2>(key) << " holds " << pm << " PM, " << pc << " PC, "
         << co << " C - not a legal possession mix";
      fail("mix", os.str());
    }
  }

  // ---- weekly allocation (rule 7) and workfronts (rule 8) -----------------
  std::map<std::tuple<ConIdx, int, Week>, std::set<int>> nights;
  std::map<std::tuple<ConIdx, int, Week, int>, std::set<ActIdx>> on_night;
  for (const auto& a : plan.accesses) {
    const auto& act = inst.activities[a.activity];
    const int t = static_cast<int>(inst.ContractOf(act).access_type);
    nights[{act.contract, t, a.week}].insert(a.access_night);
    on_night[{act.contract, t, a.week, a.access_night}].insert(a.activity);
  }
  for (const auto& [key, ns] : nights) {
    const auto& c = inst.contracts[std::get<0>(key)];
    if (static_cast<int>(ns.size()) > c.max_access_per_week) {
      std::ostringstream os;
      os << "wk" << std::get<2>(key) << ": contract " << c.number << " uses " << ns.size()
         << " access-nights against an allowance of " << c.max_access_per_week;
      fail("allocation", os.str());
    }
    for (int n : ns)
      if (n < 1 || n > c.max_access_per_week) {
        std::ostringstream os;
        os << "wk" << std::get<2>(key) << ": contract " << c.number << " uses access_night " << n
           << ", outside 1.." << c.max_access_per_week;
        fail("allocation", os.str());
      }
  }
  for (const auto& [key, acts] : on_night) {
    const auto& c = inst.contracts[std::get<0>(key)];
    if (static_cast<int>(acts.size()) > c.number_of_workfronts) {
      std::ostringstream os;
      os << "wk" << std::get<2>(key) << ": contract " << c.number << " night "
         << std::get<3>(key) << " runs " << acts.size() << " activities against "
         << c.number_of_workfronts << " workfronts";
      fail("workfront", os.str());
    }
  }

  // ---- closures and buffers (rule 4 / R6) ---------------------------------
  // Computed here from the EMITTED occupancy and its actual co_share_group
  // values. The solver's precomputed `exclusive_pairs` is deliberately NOT used:
  // consuming it would make this check repeat whatever modelling error produced
  // it, and the whole point of a separate checker is that it cannot.
  {
    // Which activities actually appear in each week, and where.
    std::map<Week, std::map<ActIdx, std::map<LocIdx, int>>> present;
    for (const auto& [key, m] : plan.slots)
      for (const auto& [act, slot] : m) present[key.second][act][key.first] = slot;

    auto zone_of = [&](ActIdx a, bool with_buffer) -> const std::vector<LocIdx>& {
      return with_buffer ? inst.activities[a].buffer_zone : inst.activities[a].closure;
    };
    auto sorted_hit = [](const std::vector<LocIdx>& zone, const std::vector<LocIdx>& own,
                         const std::vector<LocIdx>& other, LocIdx* where) {
      for (LocIdx l : zone) {
        if (std::binary_search(own.begin(), own.end(), l)) continue;
        if (std::binary_search(other.begin(), other.end(), l)) { *where = l; return true; }
      }
      return false;
    };

    for (const auto& [w, acts] : present) {
      for (auto i = acts.begin(); i != acts.end(); ++i) {
        for (auto j = std::next(i); j != acts.end(); ++j) {
          const ActIdx a = i->first, b = j->first;
          const auto& A = inst.activities[a];
          const auto& B = inst.activities[b];

          // Possession grouping, read from the file rather than assumed. Two
          // activities are one possession only where they are recorded in the
          // SAME slot at a location they both occupy (rule 6, first clause).
          bool same_possession = false, different_nights = false;
          for (const auto& [loc, slot_a] : i->second) {
            auto it = j->second.find(loc);
            if (it == j->second.end()) continue;
            if (it->second == slot_a) same_possession = true;
            else different_nights = true;
          }
          // NOTE: co_share_group carries no meaning ACROSS locations. The brief
          // calls it "an arbitrary label ... identifying which possession
          // location the activity occupies", and the shipped sample puts the
          // same pair in one slot at one location and different slots at
          // another 45 times. So it identifies a slot at its own location and
          // nothing more, and no cross-location night can be inferred from it.
          // Requiring consistency across locations would reject the sample.
          if (same_possession) continue;     // one possession here: exempt by rule 6
          if (different_nights) continue;    // separate slots here (R6c)
          (void)different_nights;

          // No shared location, so nothing establishes that they are apart.
          const bool both_buffered = A.carries_buffer && B.carries_buffer;
          LocIdx where = kNoIndex;
          bool clash = sorted_hit(zone_of(a, false), A.occupied, B.occupied, &where) ||
                       sorted_hit(zone_of(b, false), B.occupied, A.occupied, &where);
          if (!clash && both_buffered)
            clash = sorted_hit(zone_of(a, true), A.occupied, B.occupied, &where) ||
                    sorted_hit(zone_of(b, true), B.occupied, A.occupied, &where);

          // "Buffers never overlap" read literally: two buffered possessions
          // whose exclusion zones intersect at all are too close, even where
          // neither zone reaches the other's worksite. Enforced only on request:
          // the shipped sample breaches it seven times, AND enforcing it makes
          // the public instance unschedulable outright (Scenarios A and B are
          // then proven infeasible). See docs/DERIVED_RULES.md R6d.
          if (!clash && both_buffered && opts.no_zone_overlap) {
            const auto& za = A.buffer_zone;
            const auto& zb = B.buffer_zone;
            size_t x = 0, y = 0;
            while (x < za.size() && y < zb.size()) {
              if (za[x] == zb[y]) { clash = true; where = za[x]; break; }
              if (za[x] < zb[y]) ++x; else ++y;
            }
            if (clash) {
              std::ostringstream os;
              os << "wk" << w << ": exclusion zones of " << A.id << " and " << B.id
                 << " overlap at " << inst.locations[where].id;
              fail("buffer_overlap", os.str());
              continue;
            }
          }
          if (clash) {
            std::ostringstream os;
            os << "wk" << w << ": " << B.id << " inside closure of [" << A.id << "] at ["
               << (where == kNoIndex ? std::string("?") : inst.locations[where].id) << "]";
            fail("closure", os.str());
          }
        }
      }
    }
  }

  // ---- ECLO (rules 9, 10 and scenario A's prohibition) --------------------
  if (plan.scenario == Scenario::kA) {
    for (const auto& a : plan.accesses)
      if (a.eclo)
        fail("eclo", "wk" + std::to_string(a.week) + ": " + inst.activities[a.activity].id +
                         " uses ECLO, which Scenario A forbids outright");
  } else if (plan.scenario == Scenario::kC) {
    // Per line, every ECLO night must lie in one continuous span of <= 2 weeks.
    std::map<std::string, std::pair<Week, Week>> span;   // line -> (min,max)
    for (const auto& a : plan.accesses) {
      if (!a.eclo) continue;
      std::set<std::string> lines;
      for (LocIdx l : inst.activities[a.activity].closure) lines.insert(inst.locations[l].line);
      for (const auto& ln : lines) {
        auto it = span.find(ln);
        if (it == span.end()) span[ln] = {a.week, a.week};
        else { it->second.first = std::min(it->second.first, a.week);
               it->second.second = std::max(it->second.second, a.week); }
      }
    }
    for (const auto& [ln, mm] : span)
      if (mm.second - mm.first + 1 > 2) {
        std::ostringstream os;
        os << "line " << ln << ": ECLO nights span weeks " << mm.first << ".." << mm.second
           << ", exceeding the 2-week continuity window Scenario C allows";
        fail("eclo", os.str());
      }
  }

  // ---- Scenario B: planned dates are hard -------------------------------
  if (plan.scenario == Scenario::kB) {
    const auto clast = ContractLastWeek(inst, plan);
    for (size_t c = 0; c < inst.contracts.size(); ++c) {
      if (clast[c] == 0) continue;
      const Date sim = inst.SundayOfWeek(clast[c]);
      if (inst.contracts[c].planned_completion_date < sim) {
        std::ostringstream os;
        os << "contract " << inst.contracts[c].number << " completes " << sim.ToIso()
           << ", past its planned " << inst.contracts[c].planned_completion_date.ToIso()
           << ", which Scenario B fixes";
        fail("planned_date", os.str());
      }
    }
  }

  rep.soft_scores = ComputeScore(inst, plan);
  rep.feasible = rep.hard_violations.empty();
  std::sort(rep.capacity_hotspots.begin(), rep.capacity_hotspots.end());
  return rep;
}

std::string ValidationReport::ToJson(const Instance& inst) const {
  std::ostringstream os;
  os << "{\n  \"scenario\": \"" << ToString(scenario) << "\",\n";
  os << "  \"feasible\": " << (feasible ? "true" : "false") << ",\n";
  os << "  \"checker\": \"" << checker_version
     << "\",\n  \"checker_is_official_validator\": false,\n";
  os << "  \"rule6_reading\": \"" << (strict_buffers ? "literal (strict)" : "adopted")
     << "\",\n";
  os << "  \"supply_overrides\": " << supply_overrides
     << ",\n  \"produced_in_fallback_mode\": " << (fallback ? "true" : "false") << ",\n";
  os << "  \"input_hash\": \"" << inst.input_hash << "\",\n";
  os << "  \"hard_violations\": [";
  for (size_t i = 0; i < hard_violations.size(); ++i) {
    os << (i ? ",\n    " : "\n    ");
    os << "{\"rule\": \"" << hard_violations[i].rule << "\", \"severity\": \""
       << hard_violations[i].severity << "\", \"detail\": \""
       << JsonEscape(hard_violations[i].detail) << "\"}";
  }
  os << (hard_violations.empty() ? "" : "\n  ") << "],\n";
  os << "  \"soft_scores\": {\n";
  os << "    \"overrun_days_total\": " << soft_scores.overrun_days_total << ",\n";
  os << "    \"contracts_overrunning\": " << soft_scores.contracts_overrunning << ",\n";
  os << "    \"earliness_days_total\": " << soft_scores.earliness_days_total << ",\n";
  os << "    \"excess_access_nights_total\": " << soft_scores.excess_access_nights_total << ",\n";
  os << "    \"eclo_nights_total\": " << soft_scores.eclo_nights_total << ",\n";
  os << "    \"priority_overrun\": {";
  { bool first = true;
    for (int tier : {1, 2, 3}) {
      auto it = soft_scores.priority_overrun.find(tier);
      os << (first ? "" : ", ") << "\"" << tier << "\": " << (it == soft_scores.priority_overrun.end() ? 0 : it->second);
      first = false;
    } }
  os << "},\n";
  os << "    \"priority_weighted_score\": " << (soft_scores.priority_weighted_tenths / 10) << "."
     << (soft_scores.priority_weighted_tenths % 10);
  if (feasible) {
    os << ",\n    \"objective_score\": " << (soft_scores.objective_tenths / 10) << "."
       << (soft_scores.objective_tenths % 10)
       << ",\n    \"formula_version\": \"PS1-README@966c976\"";
  }
  os << "\n  },\n";
  os << "  \"detail\": {\"nights_scheduled\": " << soft_scores.nights_scheduled
     << ", \"eclo_nights\": " << soft_scores.eclo_nights_total
     << ", \"capacity_hotspots\": " << capacity_hotspots.size() << "}\n}";
  return os.str();
}

}  // namespace ta
