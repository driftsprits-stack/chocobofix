#include "core/instance.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace ta {

const char* const kInstanceFiles[8] = {
    "01_LINES.csv",  "02_STATIONS.csv",       "03_SECTORS.csv",
    "04_LOCATION_SUPPLY.csv", "05_BUFFER_LOCATION.csv", "06_PARAMETERS.csv",
    "07_PROJECT_DETAILS.csv", "08_ACTIVITY_DETAILS.csv"};

// --- date helpers (Howard Hinnant's civil calendar algorithms) --------------
namespace {

std::int64_t DaysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void CivilFromDays(std::int64_t z, int* y, unsigned* m, unsigned* d) {
  z += 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t yy = static_cast<std::int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp + (mp < 10 ? 3 : -9);
  *y = static_cast<int>(yy + (*m <= 2));
}

bool IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

unsigned DaysInMonth(int y, unsigned m) {
  static const unsigned k[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2 && IsLeap(y)) return 29;
  return k[m - 1];
}

}  // namespace

std::optional<Date> Date::Parse(std::string_view s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;
  auto num = [&](size_t off, size_t len, int* out) {
    int v = 0;
    for (size_t i = 0; i < len; ++i) {
      const char c = s[off + i];
      if (c < '0' || c > '9') return false;
      v = v * 10 + (c - '0');
    }
    *out = v;
    return true;
  };
  int y = 0, m = 0, d = 0;
  if (!num(0, 4, &y) || !num(5, 2, &m) || !num(8, 2, &d)) return std::nullopt;
  if (m < 1 || m > 12 || y < 1900 || y > 2999) return std::nullopt;
  if (d < 1 || static_cast<unsigned>(d) > DaysInMonth(y, static_cast<unsigned>(m))) return std::nullopt;
  return Date{DaysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d))};
}

std::string Date::ToIso() const {
  int y; unsigned m, d;
  CivilFromDays(days, &y, &m, &d);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d);
  return buf;
}

Week Instance::WeekOf(Date d) const {
  const std::int64_t off = d.days - horizon_start.days;
  const std::int64_t w = (off >= 0 ? off / 7 : (off - 6) / 7) + 1;
  return static_cast<Week>(std::clamp<std::int64_t>(w, 1, horizon_weeks));
}

Date Instance::SundayOfWeek(Week w) const {
  return Date{horizon_start.days + static_cast<std::int64_t>(w - 1) * 7 + 6};
}

// --- enums -----------------------------------------------------------------
std::string_view ToString(AccessType t) {
  switch (t) { case AccessType::kPM: return "PM"; case AccessType::kPC: return "PC"; default: return "C"; }
}
std::string_view ToString(Nature n) {
  switch (n) {
    case Nature::kLive: return "Live";
    case Nature::kNonLiveConsist: return "Non-live (Consist)";
    default: return "Non-live (Others)";
  }
}
std::string_view ToString(Scenario s) {
  switch (s) { case Scenario::kA: return "A"; case Scenario::kB: return "B"; default: return "C"; }
}
std::optional<AccessType> ParseAccessType(std::string_view s) {
  if (s == "PM") return AccessType::kPM;
  if (s == "PC") return AccessType::kPC;
  if (s == "C") return AccessType::kC;
  return std::nullopt;
}
std::optional<Nature> ParseNature(std::string_view s) {
  if (s == "Live") return Nature::kLive;
  if (s == "Non-live (Consist)") return Nature::kNonLiveConsist;
  if (s == "Non-live (Others)") return Nature::kNonLiveOthers;
  return std::nullopt;
}
std::optional<Scenario> ParseScenario(std::string_view s) {
  if (s == "A") return Scenario::kA;
  if (s == "B") return Scenario::kB;
  if (s == "C") return Scenario::kC;
  return std::nullopt;
}

// --- topology --------------------------------------------------------------
namespace {

struct StationRow { std::string id; std::string line; int seq; bool interchange; };
struct SectorRow { std::string id; std::string line; std::string from, to; int seq; };

// Parses "SEC:ALP:S02_S03:EB" / "PLAT:ALP:S03:EB".
bool SplitLocationId(const std::string& id, std::string* kind, std::string* line,
                     std::string* body, std::string* bound) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : id) { if (c == ':') { parts.push_back(cur); cur.clear(); } else cur.push_back(c); }
  parts.push_back(cur);
  if (parts.size() != 4) return false;
  *kind = parts[0]; *line = parts[1]; *body = parts[2]; *bound = parts[3];
  return (*kind == "SEC" || *kind == "PLAT") && (*bound == "EB" || *bound == "WB");
}

}  // namespace

// Rebuilt separately from loading so tests can drive it with synthetic networks.
bool BuildTopology(Instance* inst, std::vector<InputError>* errors) {
  const size_t before = errors->size();

  // Per (line,bound) alternating chain: PLAT s0, SEC s0_s1, PLAT s1, ...
  // Sector ordinals let the buffer grow by *sectors* (R6) rather than by raw
  // chain steps, which the sample refutes.
  std::map<std::pair<std::string, std::string>, std::vector<LocIdx>> chains;
  for (LocIdx i = 0; i < static_cast<LocIdx>(inst->locations.size()); ++i) {
    const auto& L = inst->locations[i];
    chains[{L.line, L.bound == Bound::kEB ? "EB" : "WB"}].push_back(i);
  }
  for (auto& [key, ids] : chains) {
    // Order: station seq for platforms, sector seq for sectors, interleaved.
    std::sort(ids.begin(), ids.end(), [&](LocIdx a, LocIdx b) {
      return inst->locations[a].chain_index < inst->locations[b].chain_index;
    });
    int sector_count = 0;
    for (size_t k = 0; k < ids.size(); ++k) {
      auto& L = inst->locations[ids[k]];
      L.chain_index = static_cast<int>(k);
      if (L.kind == LocationKind::kTunnelSector) ++sector_count;
      L.sector_ordinal = sector_count;   // sectors seen up to and including this slot
    }
  }

  // --- expand each activity's span ----------------------------------------
  inst->activities_at.assign(inst->locations.size(), {});
  for (auto& a : inst->activities) {
    std::string k1, l1, b1, bd1, k2, l2, b2, bd2;
    if (!SplitLocationId(a.start_location_id, &k1, &l1, &b1, &bd1) ||
        !SplitLocationId(a.end_location_id, &k2, &l2, &b2, &bd2)) {
      errors->push_back({"08_ACTIVITY_DETAILS.csv", 0, "start_location_id",
                         a.id + ": location id is not of the form SEC:<line>:<a>_<b>:<bound>"});
      continue;
    }
    if (k1 != "SEC" || k2 != "SEC") {
      errors->push_back({"08_ACTIVITY_DETAILS.csv", 0, "start_location_id",
                         a.id + ": a work span must be given between tunnel sectors"});
      continue;
    }
    if (l1 != l2 || bd1 != bd2) {
      errors->push_back({"08_ACTIVITY_DETAILS.csv", 0, "end_location_id",
                         a.id + ": span crosses line or bound (" + a.start_location_id +
                             " -> " + a.end_location_id + ")"});
      continue;
    }
    auto s_it = inst->location_by_id.find(a.start_location_id);
    auto e_it = inst->location_by_id.find(a.end_location_id);
    if (s_it == inst->location_by_id.end() || e_it == inst->location_by_id.end()) {
      errors->push_back({"08_ACTIVITY_DETAILS.csv", 0, "start_location_id",
                         a.id + ": references a location absent from 04_LOCATION_SUPPLY.csv"});
      continue;
    }
    const auto& chain = chains[{l1, bd1}];
    int lo = inst->locations[s_it->second].chain_index;
    int hi = inst->locations[e_it->second].chain_index;
    if (lo > hi) std::swap(lo, hi);
    // R1: the span runs from the first sector's entry platform to the last
    // sector's exit platform, so widen one chain slot at each end.
    lo = std::max(0, lo - 1);
    hi = std::min<int>(static_cast<int>(chain.size()) - 1, hi + 1);
    for (int k = lo; k <= hi; ++k) a.occupied.push_back(chain[k]);
    std::sort(a.occupied.begin(), a.occupied.end());
    for (LocIdx l : a.occupied) inst->activities_at[l].push_back(&a - inst->activities.data());
  }
  if (errors->size() != before) return false;

  // --- closure and buffer zones (R6) --------------------------------------
  for (auto& a : inst->activities) {
    const auto& c = inst->ContractOf(a);
    const BufferRule br = inst->buffers.at(c.nature);
    a.carries_buffer = br.up_to_buffer_sectors > 0;
    const std::string line = inst->locations[a.occupied.front()].line;

    // Step 1: the worksite, plus - for Live - the mirrored opposite bound,
    // because cutting traction power takes both bounds out together.
    std::set<LocIdx> base(a.occupied.begin(), a.occupied.end());
    if (br.opposite_bound_required) {
      std::set<LocIdx> mirrored = base;
      for (LocIdx l : base) {
        const auto& L = inst->locations[l];
        std::string k, ln, body, bd;
        SplitLocationId(L.id, &k, &ln, &body, &bd);
        auto it = inst->location_by_id.find(k + ":" + ln + ":" + body + ":" + (bd == "EB" ? "WB" : "EB"));
        if (it != inst->location_by_id.end()) mirrored.insert(it->second);
      }
      base.swap(mirrored);
    }

    // Step 2: grow by the nature's buffer, counted in sectors along each
    // (line, bound) chain the worksite touches, stopping on the last sector.
    std::set<LocIdx> grown = base;
    if (br.up_to_buffer_sectors > 0) {
      std::set<std::pair<std::string, std::string>> spans;
      for (LocIdx l : base)
        spans.insert({inst->locations[l].line, inst->locations[l].bound == Bound::kEB ? "EB" : "WB"});
      for (const auto& [ln, bd] : spans) {
        const auto& chain = chains[{ln, bd}];
        if (chain.empty()) continue;
        int lo = -1, hi = -1;
        for (LocIdx l : base) {
          const auto& L = inst->locations[l];
          if (L.line != ln || (L.bound == Bound::kEB ? "EB" : "WB") != bd) continue;
          if (lo < 0 || L.chain_index < lo) lo = L.chain_index;
          if (L.chain_index > hi) hi = L.chain_index;
        }
        if (lo < 0) continue;
        int need = br.up_to_buffer_sectors;
        for (int k = lo - 1; k >= 0; --k) {
          grown.insert(chain[k]);
          if (inst->locations[chain[k]].kind == LocationKind::kTunnelSector && --need == 0) break;
        }
        need = br.up_to_buffer_sectors;
        for (int k = hi + 1; k < static_cast<int>(chain.size()); ++k) {
          grown.insert(chain[k]);
          if (inst->locations[chain[k]].kind == LocationKind::kTunnelSector && --need == 0) break;
        }
      }
    }

    // Step 3: the Live-only interchange crossing. This is an exact set named by
    // the brief - the other line's H01_H02 tunnel and its H01/H02 platforms -
    // and is NOT itself grown by the buffer.
    std::set<LocIdx> cross;
    if (br.opposite_bound_required) {
      bool at_interchange = false;
      for (LocIdx l : base) {
        const std::string& id = inst->locations[l].id;
        if (id.find("H01_H02") != std::string::npos || id.find(":H01:") != std::string::npos ||
            id.find(":H02:") != std::string::npos) { at_interchange = true; break; }
      }
      if (at_interchange) {
        const std::string other = (line == "ALP") ? "BET" : "ALP";
        for (const char* bd : {"EB", "WB"})
          for (const std::string& id : {"SEC:" + other + ":H01_H02:" + bd,
                                        "PLAT:" + other + ":H01:" + bd,
                                        "PLAT:" + other + ":H02:" + bd}) {
            auto it = inst->location_by_id.find(id);
            if (it != inst->location_by_id.end()) cross.insert(it->second);
          }
      }
    }
    base.insert(cross.begin(), cross.end());
    grown.insert(cross.begin(), cross.end());
    a.closure.assign(base.begin(), base.end());
    a.buffer_zone.assign(grown.begin(), grown.end());
  }

  // --- exclusive pairs (R6) ------------------------------------------------
  // Spans never move, so this is a static property of the instance.
  const int n = static_cast<int>(inst->activities.size());
  auto intersects = [](const std::vector<LocIdx>& x, const std::vector<LocIdx>& y) {
    size_t i = 0, j = 0;
    while (i < x.size() && j < y.size()) {
      if (x[i] == y[j]) return true;
      if (x[i] < y[j]) ++i; else ++j;
    }
    return false;
  };
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j) {
      const auto& A = inst->activities[i];
      const auto& B = inst->activities[j];
      // A closure excludes any other activity from the locations it closes; a
      // buffer only pushes other buffer-carrying work (docs R6a / R6b).
      bool clash = intersects(A.closure, B.occupied) || intersects(B.closure, A.occupied);
      if (!clash && A.carries_buffer && B.carries_buffer)
        clash = intersects(A.buffer_zone, B.occupied) || intersects(B.buffer_zone, A.occupied);
      if (!clash) continue;

      // Literal reading: a zone reaching into the other's worksite is a breach
      // even when the two share a location, because rule 6 exempts only an
      // identical (location, week, co_share_group).
      const bool shares_location = intersects(A.occupied, B.occupied);
      {
        // Only the part of a zone OUTSIDE the owner's own worksite can intrude;
        // overlapping worksites are governed by capacity and the legal mix.
        auto beyond = [&](const std::vector<LocIdx>& zone, const std::vector<LocIdx>& own,
                          const std::vector<LocIdx>& other) {
          for (LocIdx l : zone) {
            if (std::binary_search(own.begin(), own.end(), l)) continue;
            if (std::binary_search(other.begin(), other.end(), l)) return true;
          }
          return false;
        };
        bool strict = beyond(A.closure, A.occupied, B.occupied) ||
                      beyond(B.closure, B.occupied, A.occupied);
        if (!strict && A.carries_buffer && B.carries_buffer)
          strict = beyond(A.buffer_zone, A.occupied, B.occupied) ||
                   beyond(B.buffer_zone, B.occupied, A.occupied);
        if (strict) inst->exclusive_pairs_strict.emplace_back(i, j);
      }
      // Adopted reading: sharing a location settles the pair (identical group =
      // one possession; different group = provably different nights).
      if (!shares_location) inst->exclusive_pairs.emplace_back(i, j);
    }
  return true;
}

// --- loading ---------------------------------------------------------------
bool LoadInstance(const std::string& dir, Instance* inst, std::vector<InputError>* errors) {
  const std::string sep = "/";
  auto path = [&](const char* f) { return dir + sep + f; };

  // Bind the plan to exact input bytes: hash the eight files in canonical order.
  {
    std::string all;
    for (const char* f : kInstanceFiles) {
      std::string bytes;
      if (!ReadFile(path(f), &bytes)) {
        errors->push_back({f, 0, "", "required instance file is missing from " + dir});
        continue;
      }
      all += f;
      all.push_back('\n');
      all += Sha256Hex(bytes);
      all.push_back('\n');
    }
    if (!errors->empty()) return false;
    inst->input_hash = Sha256Hex(all);
  }

  CsvTable params, stations, sectors, supply, buffers, projects, activities;
  if (!CsvTable::Load(path("06_PARAMETERS.csv"), {"key", "value"}, &params, errors)) return false;
  if (!CsvTable::Load(path("02_STATIONS.csv"),
                      {"station_id", "line_code", "seq", "is_interchange"}, &stations, errors)) return false;
  if (!CsvTable::Load(path("03_SECTORS.csv"),
                      {"sector_id", "line_code", "from_station_id", "to_station_id", "seq"},
                      &sectors, errors)) return false;
  if (!CsvTable::Load(path("04_LOCATION_SUPPLY.csv"),
                      {"location_id", "location_kind", "line_code", "bound", "supply_capacity"},
                      &supply, errors)) return false;
  if (!CsvTable::Load(path("05_BUFFER_LOCATION.csv"),
                      {"nature_of_works", "up_to_buffer_sectors", "opposite_bound_required"},
                      &buffers, errors)) return false;
  if (!CsvTable::Load(path("07_PROJECT_DETAILS.csv"),
                      {"contract_number", "nature_of_activity", "contract_priority",
                       "contract_completion_date", "planned_completion_date",
                       "number_of_workfronts", "access_type", "number_of_maximum_access_per_week"},
                      &projects, errors)) return false;
  if (!CsvTable::Load(path("08_ACTIVITY_DETAILS.csv"),
                      {"activity_id", "contract_number", "start_location_id", "end_location_id",
                       "total_accesses", "planned_start_date", "activity_priority"},
                      &activities, errors)) return false;

  // --- parameters ---
  {
    std::map<std::string, std::string> kv;
    for (int r = 0; r < params.RowCount(); ++r)
      kv[params.Str(r, "key", errors)] = params.Str(r, "value", errors);
    auto hs = kv.count("horizon_start") ? Date::Parse(kv["horizon_start"]) : std::nullopt;
    if (!hs) { errors->push_back({"06_PARAMETERS.csv", 0, "horizon_start",
                                  "missing or not a YYYY-MM-DD date"}); return false; }
    inst->horizon_start = *hs;
    try { inst->horizon_weeks = std::stoi(kv.at("horizon_weeks")); }
    catch (...) { errors->push_back({"06_PARAMETERS.csv", 0, "horizon_weeks",
                                     "missing or not an integer"}); return false; }
    if (inst->horizon_weeks < 1 || inst->horizon_weeks > 520) {
      errors->push_back({"06_PARAMETERS.csv", 0, "horizon_weeks",
                         "outside the supported range 1..520"});
      return false;
    }
  }

  // --- buffers ---
  for (int r = 0; r < buffers.RowCount(); ++r) {
    const auto nat = ParseNature(buffers.Str(r, "nature_of_works", errors));
    if (!nat) { buffers.AddError(r + 2, "nature_of_works", "unrecognised nature of works", errors); continue; }
    inst->buffers[*nat] = BufferRule{buffers.Int(r, "up_to_buffer_sectors", errors, 0, 20),
                                     buffers.Int(r, "opposite_bound_required", errors, 0, 1) == 1};
  }
  for (Nature n : {Nature::kLive, Nature::kNonLiveConsist, Nature::kNonLiveOthers})
    if (!inst->buffers.count(n))
      errors->push_back({"05_BUFFER_LOCATION.csv", 0, "nature_of_works",
                         std::string("no buffer row for \"") + std::string(ToString(n)) + "\""});

  // --- network ordering, used to place locations on their chain ---
  std::map<std::pair<std::string, std::string>, int> station_seq;   // (line,station)->seq
  for (int r = 0; r < stations.RowCount(); ++r)
    station_seq[{stations.Str(r, "line_code", errors), stations.Str(r, "station_id", errors)}] =
        stations.Int(r, "seq", errors, 1, 100000);
  std::map<std::string, std::pair<std::string, std::string>> sector_ends;  // sector_id -> (from,to)
  for (int r = 0; r < sectors.RowCount(); ++r)
    sector_ends[sectors.Str(r, "sector_id", errors)] =
        {sectors.Str(r, "from_station_id", errors), sectors.Str(r, "to_station_id", errors)};

  // --- locations ---
  for (int r = 0; r < supply.RowCount(); ++r) {
    Location L;
    L.id = supply.Str(r, "location_id", errors);
    const std::string kind = supply.Str(r, "location_kind", errors);
    if (kind == "tunnel sector") L.kind = LocationKind::kTunnelSector;
    else if (kind == "platform sector") L.kind = LocationKind::kPlatformSector;
    else { supply.AddError(r + 2, "location_kind", "expected \"tunnel sector\" or \"platform sector\"", errors); continue; }
    L.line = supply.Str(r, "line_code", errors);
    const std::string bd = supply.Str(r, "bound", errors);
    if (bd == "EB") L.bound = Bound::kEB;
    else if (bd == "WB") L.bound = Bound::kWB;
    else { supply.AddError(r + 2, "bound", "expected EB or WB", errors); continue; }
    L.supply_capacity = supply.Int(r, "supply_capacity", errors, 0, 1000);

    // Chain position: platform at station k sits at 2k, the sector leaving it at 2k+1.
    std::string k2, l2, body, bd2;
    if (!SplitLocationId(L.id, &k2, &l2, &body, &bd2)) {
      supply.AddError(r + 2, "location_id", "malformed location id", errors); continue;
    }
    if (L.kind == LocationKind::kPlatformSector) {
      auto it = station_seq.find({L.line, body});
      if (it == station_seq.end()) {
        supply.AddError(r + 2, "location_id", "platform references a station absent from 02_STATIONS.csv", errors);
        continue;
      }
      L.chain_index = 2 * (it->second - 1);
    } else {
      auto it = sector_ends.find(k2 + ":" + l2 + ":" + body);
      if (it == sector_ends.end()) {
        supply.AddError(r + 2, "location_id", "tunnel sector absent from 03_SECTORS.csv", errors);
        continue;
      }
      auto sit = station_seq.find({L.line, it->second.first});
      if (sit == station_seq.end()) {
        supply.AddError(r + 2, "location_id", "sector's from_station absent from 02_STATIONS.csv", errors);
        continue;
      }
      L.chain_index = 2 * (sit->second - 1) + 1;
    }
    if (inst->location_by_id.count(L.id)) {
      supply.AddError(r + 2, "location_id", "duplicate location id \"" + L.id + "\"", errors);
      continue;
    }
    inst->location_by_id[L.id] = static_cast<LocIdx>(inst->locations.size());
    inst->locations.push_back(std::move(L));
  }

  // --- contracts ---
  for (int r = 0; r < projects.RowCount(); ++r) {
    Contract c;
    c.number = projects.Str(r, "contract_number", errors);
    c.description = projects.OptStr(r, "contract_description");
    c.activity_type = projects.OptStr(r, "activity_type");
    const auto nat = ParseNature(projects.Str(r, "nature_of_activity", errors));
    if (!nat) { projects.AddError(r + 2, "nature_of_activity", "unrecognised nature of works", errors); continue; }
    c.nature = *nat;
    c.priority = projects.Int(r, "contract_priority", errors, 1, 3);
    c.contract_completion_date = projects.DateOf(r, "contract_completion_date", errors);
    c.planned_completion_date = projects.DateOf(r, "planned_completion_date", errors);
    c.number_of_workfronts = projects.Int(r, "number_of_workfronts", errors, 1, 1000);
    const auto at = ParseAccessType(projects.Str(r, "access_type", errors));
    if (!at) { projects.AddError(r + 2, "access_type", "expected PM, PC or C", errors); continue; }
    c.access_type = *at;
    c.max_access_per_week = projects.Int(r, "number_of_maximum_access_per_week", errors, 0, 7);
    if (inst->contract_by_number.count(c.number)) {
      projects.AddError(r + 2, "contract_number", "duplicate contract \"" + c.number + "\"", errors);
      continue;
    }
    inst->contract_by_number[c.number] = static_cast<ConIdx>(inst->contracts.size());
    inst->contracts.push_back(std::move(c));
  }
  for (auto& c : inst->contracts) c.planned_completion_week = inst->WeekOf(c.planned_completion_date);

  // --- activities ---
  std::vector<std::string> pending_pred;
  for (int r = 0; r < activities.RowCount(); ++r) {
    Activity a;
    a.id = activities.Str(r, "activity_id", errors);
    const std::string cn = activities.Str(r, "contract_number", errors);
    auto cit = inst->contract_by_number.find(cn);
    if (cit == inst->contract_by_number.end()) {
      activities.AddError(r + 2, "contract_number", "\"" + cn + "\" is not in 07_PROJECT_DETAILS.csv", errors);
      continue;
    }
    a.contract = cit->second;
    a.activity_type = activities.OptStr(r, "activity_type");
    a.start_location_id = activities.Str(r, "start_location_id", errors);
    a.end_location_id = activities.Str(r, "end_location_id", errors);
    a.total_accesses = activities.Int(r, "total_accesses", errors, 0, 100000);
    a.planned_start_date = activities.DateOf(r, "planned_start_date", errors);
    a.activity_priority = activities.Int(r, "activity_priority", errors, 1, 3);
    a.earliest_week = inst->WeekOf(a.planned_start_date);
    if (inst->activity_by_id.count(a.id)) {
      activities.AddError(r + 2, "activity_id", "duplicate activity \"" + a.id + "\"", errors);
      continue;
    }
    inst->activity_by_id[a.id] = static_cast<ActIdx>(inst->activities.size());
    pending_pred.push_back(activities.OptStr(r, "predecessor_activity_id"));
    inst->activities.push_back(std::move(a));
  }
  for (size_t i = 0; i < pending_pred.size(); ++i) {
    if (pending_pred[i].empty()) continue;
    auto it = inst->activity_by_id.find(pending_pred[i]);
    if (it == inst->activity_by_id.end()) {
      errors->push_back({"08_ACTIVITY_DETAILS.csv", static_cast<int>(i + 2), "predecessor_activity_id",
                         inst->activities[i].id + ": predecessor \"" + pending_pred[i] + "\" does not exist"});
      continue;
    }
    if (static_cast<ActIdx>(i) == it->second) {
      errors->push_back({"08_ACTIVITY_DETAILS.csv", static_cast<int>(i + 2), "predecessor_activity_id",
                         inst->activities[i].id + ": an activity cannot be its own predecessor"});
      continue;
    }
    inst->activities[i].predecessor = it->second;
  }

  // Predecessor cycles are an inconsistent *input*, distinct from a hard search.
  {
    const int n = static_cast<int>(inst->activities.size());
    std::vector<int> state(n, 0);   // 0 unvisited, 1 on stack, 2 done
    for (int s = 0; s < n; ++s) {
      int cur = s;
      std::vector<int> stack;
      while (cur != kNoIndex && state[cur] == 0) {
        state[cur] = 1; stack.push_back(cur); cur = inst->activities[cur].predecessor;
      }
      if (cur != kNoIndex && state[cur] == 1) {
        std::ostringstream os;
        os << "predecessor cycle detected involving " << inst->activities[cur].id;
        errors->push_back({"08_ACTIVITY_DETAILS.csv", 0, "predecessor_activity_id", os.str()});
        for (int v : stack) state[v] = 2;
        break;
      }
      for (int v : stack) state[v] = 2;
    }
  }

  if (!errors->empty()) return false;
  return BuildTopology(inst, errors);
}

}  // namespace ta
