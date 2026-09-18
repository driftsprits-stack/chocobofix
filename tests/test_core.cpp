// Behavioural tests for the scheduling domain.
//
// The emphasis is on rules that are expensive to get wrong: date/week mapping,
// span expansion, buffer geometry, slot packing, scoring arithmetic, and - most
// importantly - that the checker REJECTS schedules it should reject. A checker
// that only ever says yes proves nothing.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <sstream>

#include "core/instance.h"
#include "core/schedule.h"
#include "validator/validator.h"

namespace fs = std::filesystem;
using namespace ta;

namespace {

int g_pass = 0, g_fail = 0;
std::string g_group;

void Group(const char* g) { g_group = g; }
void Check(bool ok, const std::string& what) {
  if (ok) { ++g_pass; }
  else { ++g_fail; std::cout << "  FAIL [" << g_group << "] " << what << "\n"; }
}
template <typename A, typename B>
void Eq(const A& a, const B& b, const std::string& what) {
  if (a == b) { ++g_pass; return; }
  ++g_fail;
  std::ostringstream os;
  os << what << "  (got " << a << ", expected " << b << ")";
  std::cout << "  FAIL [" << g_group << "] " << os.str() << "\n";
}

const char* kData = "data/upstream/PS1/01_data";
const char* kSample = "data/upstream/PS1/03_submission_sample";

// Copies the instance to a scratch directory so a test may perturb it.
std::string CopyInstance(const std::string& tag) {
  const std::string dir = (fs::temp_directory_path() / ("ta_test_" + tag)).string();
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  for (const char* f : kInstanceFiles)
    fs::copy_file(std::string(kData) + "/" + f, dir + "/" + f, fs::copy_options::overwrite_existing, ec);
  return dir;
}

std::vector<std::string> ReadLines(const std::string& p) {
  std::ifstream in(p);
  std::vector<std::string> out;
  std::string l;
  while (std::getline(in, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); out.push_back(l); }
  return out;
}
void WriteLines(const std::string& p, const std::vector<std::string>& lines) {
  std::ofstream out(p, std::ios::trunc);
  for (const auto& l : lines) out << l << "\n";
}

bool LoadOk(const std::string& dir, Instance* inst) {
  std::vector<InputError> errs;
  return LoadInstance(dir, inst, &errs);
}

// Counts hard violations carrying a given rule tag.
int CountRule(const ValidationReport& r, const std::string& tag) {
  int n = 0;
  for (const auto& v : r.hard_violations) if (v.rule == tag) ++n;
  return n;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------- SHA-256
  Group("sha256");
  Eq(Sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
     "empty string known-answer");
  Eq(Sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
     "\"abc\" known-answer");
  Eq(Sha256Hex(std::string(1000, 'a')).size(), size_t(64), "digest length for a multi-block input");

  // ---------------------------------------------------------------- dates
  Group("dates");
  Check(Date::Parse("2027-01-04").has_value(), "a valid ISO date parses");
  Check(!Date::Parse("2027-02-30").has_value(), "30 February is rejected");
  Check(!Date::Parse("2027-13-01").has_value(), "month 13 is rejected");
  Check(!Date::Parse("2027-1-4").has_value(), "non-padded form is rejected");
  Check(!Date::Parse("").has_value(), "empty string is rejected");
  Eq(Date::Parse("2027-01-04")->ToIso(), std::string("2027-01-04"), "date round-trips");
  Eq(Date::Parse("2028-02-29")->ToIso(), std::string("2028-02-29"), "leap day round-trips");
  Eq(Date::Parse("2027-03-01")->days - Date::Parse("2027-02-28")->days, 1LL,
     "2027 is not a leap year");

  // ---------------------------------------------------------------- instance
  Group("instance");
  Instance inst;
  Check(LoadOk(kData, &inst), "the public instance loads");
  Eq(inst.activities.size(), size_t(54), "activity count");
  Eq(inst.contracts.size(), size_t(14), "contract count");
  Eq(inst.locations.size(), size_t(76), "location count");
  Eq(inst.horizon_weeks, 30, "horizon weeks");
  Eq(inst.horizon_start.ToIso(), std::string("2027-01-04"), "horizon start");

  Group("week mapping");
  Eq(inst.WeekOf(*Date::Parse("2027-01-04")), 1, "horizon start is week 1");
  Eq(inst.WeekOf(*Date::Parse("2027-01-10")), 1, "the Sunday after start is still week 1");
  Eq(inst.WeekOf(*Date::Parse("2027-01-11")), 2, "the next Monday is week 2");
  Eq(inst.SundayOfWeek(1).ToIso(), std::string("2027-01-10"), "week 1 ends on its Sunday");
  // The completion-date formula, pinned against the shipped RESULTS.csv.
  Eq(inst.SundayOfWeek(28).ToIso(), std::string("2027-07-18"), "week 28 Sunday matches the sample");
  Eq(inst.SundayOfWeek(23).ToIso(), std::string("2027-06-13"), "week 23 Sunday matches the sample");

  // ---------------------------------------------------------------- topology
  Group("span expansion");
  {
    // A001 runs SEC:BET:S15_S16:EB -> SEC:BET:S16_S17:EB. Endpoint platforms are
    // included, so it books S15, S16 and S17 plus the two sectors: 5 locations.
    const auto& a = inst.activities[inst.activity_by_id.at("A001")];
    Eq(a.occupied.size(), size_t(5), "A001 books five locations");
    std::vector<std::string> ids;
    for (LocIdx l : a.occupied) ids.push_back(inst.locations[l].id);
    std::sort(ids.begin(), ids.end());
    const std::vector<std::string> want = {
        "PLAT:BET:S15:EB", "PLAT:BET:S16:EB", "PLAT:BET:S17:EB",
        "SEC:BET:S15_S16:EB", "SEC:BET:S16_S17:EB"};
    Check(ids == want, "A001 books exactly the expected locations");

    // A single-sector activity books that sector plus both its platforms.
    const auto& b = inst.activities[inst.activity_by_id.at("A007")];  // H01_H02 only
    Eq(b.occupied.size(), size_t(3), "a single-sector span books three locations");
  }

  Group("buffer geometry");
  {
    // Non-live (Others) carries no buffer: both zones equal the worksite.
    for (const auto& a : inst.activities)
      if (inst.ContractOf(a).nature == Nature::kNonLiveOthers) {
        Check(!a.carries_buffer, a.id + " (Non-live (Others)) carries no buffer");
        Eq(a.buffer_zone.size(), a.occupied.size(),
           a.id + " (Non-live (Others)) has no zone beyond its worksite");
        break;
      }
    // Non-live (Consist) reaches exactly one further sector each side. The
    // closure proper is the worksite; the buffer zone adds the two sectors.
    const auto& c = inst.activities[inst.activity_by_id.at("A001")];
    Check(inst.ContractOf(c).nature == Nature::kNonLiveConsist, "A001 is a Consist activity");
    Check(c.carries_buffer, "A001 carries a buffer");
    Eq(c.closure.size(), c.occupied.size(), "a Consist closure is its own worksite");
    Eq(c.buffer_zone.size(), c.occupied.size() + 2, "a 1-sector buffer adds one sector each side");
    // Live mirrors onto the opposite bound and crosses lines at the interchange.
    const auto& live = inst.activities[inst.activity_by_id.at("A074")];
    Check(inst.ContractOf(live).nature == Nature::kLive, "A074 is a Live activity");
    bool has_opposite = false, has_other_line = false;
    for (LocIdx l : live.closure) {
      const auto& L = inst.locations[l];
      if (L.bound == Bound::kWB) has_opposite = true;      // A074 works the EB bound
      if (L.line == "BET") has_other_line = true;          // A074 is on ALP
    }
    Check(has_opposite, "a Live closure mirrors onto the opposite bound");
    Check(has_other_line, "a Live closure at the interchange crosses to the other line");
  }

  // ---------------------------------------------------------------- the sample
  Group("sample corroboration");
  Plan sample;
  {
    std::vector<InputError> errs;
    Check(LoadPlan(inst, kSample, &sample, &errs), "the shipped sample parses");
    const auto rep = Validate(inst, sample);
    Check(rep.feasible, "the shipped sample validates as feasible (0 hard violations)");
    Eq(rep.hard_violations.size(), size_t(0), "sample hard violation count");
    Eq(rep.soft_scores.overrun_days_total, 28, "sample overrun days");
    Eq(rep.soft_scores.contracts_overrunning, 3, "sample overrunning contracts");
    Eq(rep.soft_scores.priority_weighted_tenths, 483LL, "sample weighted score x10");
    Eq(rep.soft_scores.nights_scheduled, 192, "sample nights scheduled");
  }

  // ------------------------------------------------- the checker must say NO
  // Each mutation breaks exactly one rule. A checker that passes these is not
  // checking anything.
  Group("mutation: the checker must reject");
  {
    Plan p = sample;                       // drop one access-night
    p.accesses.pop_back();
    const auto r = Validate(inst, p);
    Check(!r.feasible, "dropping an access-night is rejected");
    Check(CountRule(r, "workload") > 0 || CountRule(r, "occupancy") > 0,
          "  ... and is reported as a workload or occupancy breach");
  }
  {
    Plan p = sample;                       // start before the planned start week
    p.accesses[0].week = 1;
    const auto r = Validate(inst, p);
    Check(CountRule(r, "planned_start") > 0 || CountRule(r, "occupancy") > 0,
          "starting before the planned start week is rejected");
  }
  {
    Plan p = sample;                       // ECLO under Scenario A
    p.scenario = Scenario::kA;
    p.accesses[0].eclo = true;
    const auto r = Validate(inst, p);
    Check(CountRule(r, "eclo") > 0, "an ECLO night under Scenario A is rejected");
  }
  {
    Plan p = sample;                       // exceed a contract's weekly allowance
    for (auto& a : p.accesses) a.access_night = 7;
    const auto r = Validate(inst, p);
    Check(CountRule(r, "allocation") > 0, "an out-of-range access_night is rejected");
  }
  {
    Plan p = sample;                       // overfill one location-week
    for (auto& [key, used] : p.slots_used) { used = inst.locations[key.first].supply_capacity + 3; break; }
    const auto r = Validate(inst, p);
    Check(CountRule(r, "capacity") > 0, "exceeding a location's weekly supply is rejected");
  }
  {
    Plan p = sample;                       // put a PM in a slot with other work
    ActIdx pm = kNoIndex, co = kNoIndex;
    for (ActIdx i = 0; i < static_cast<ActIdx>(inst.activities.size()); ++i) {
      const auto t = inst.ContractOf(inst.activities[i]).access_type;
      if (t == AccessType::kPM && pm == kNoIndex) pm = i;
      if (t == AccessType::kC && co == kNoIndex) co = i;
    }
    Check(pm != kNoIndex && co != kNoIndex, "the instance has both a PM and a C contract");
    for (auto& [key, m] : p.slots) { m[pm] = 1; m[co] = 1; break; }
    const auto r = Validate(inst, p);
    Check(CountRule(r, "mix") > 0, "a PM sharing a slot with other work is rejected");
  }
  {
    Plan p = sample;                       // break a predecessor relation
    ActIdx succ = kNoIndex;
    for (ActIdx i = 0; i < static_cast<ActIdx>(inst.activities.size()); ++i)
      if (inst.activities[i].predecessor != kNoIndex) { succ = i; break; }
    Check(succ != kNoIndex, "the instance has a predecessor link");
    const ActIdx pred = inst.activities[succ].predecessor;
    Week pred_last = 0;
    for (const auto& a : p.accesses) if (a.activity == pred) pred_last = std::max(pred_last, a.week);
    for (auto& a : p.accesses) if (a.activity == succ) a.week = pred_last;   // same week, not later
    const auto r = Validate(inst, p);
    Check(CountRule(r, "precedence") > 0, "a successor in the predecessor's own week is rejected");
  }
  {
    Plan p = sample;                       // two accesses for one activity in a week
    Access dup = p.accesses[0];
    p.accesses.push_back(dup);
    const auto r = Validate(inst, p);
    Check(!r.feasible, "a second access for one activity in one week is rejected");
  }
  {
    Plan p = sample;                       // Scenario B with a contract overrunning
    p.scenario = Scenario::kB;
    const auto r = Validate(inst, p);
    Check(CountRule(r, "planned_date") > 0,
          "overrunning a planned date under Scenario B is rejected");
  }

  // ---------------------------------------------------------------- metamorphic
  Group("metamorphic");
  {
    // Reordering data rows must not change the derived model. The loader keys on
    // identifiers, so row order is not allowed to carry meaning.
    const std::string dir = CopyInstance("shuffled");
    auto lines = ReadLines(dir + "/08_ACTIVITY_DETAILS.csv");
    std::vector<std::string> body(lines.begin() + 1, lines.end());
    std::reverse(body.begin(), body.end());
    std::vector<std::string> out{lines[0]};
    out.insert(out.end(), body.begin(), body.end());
    WriteLines(dir + "/08_ACTIVITY_DETAILS.csv", out);

    Instance shuffled;
    Check(LoadOk(dir, &shuffled), "an instance with reversed activity rows still loads");
    Eq(shuffled.activities.size(), inst.activities.size(), "same activity count after reordering");
    // Compare expansions by identifier, not by index.
    bool same = true;
    for (const auto& a : inst.activities) {
      const auto& b = shuffled.activities[shuffled.activity_by_id.at(a.id)];
      if (a.occupied.size() != b.occupied.size() || a.closure.size() != b.closure.size()) same = false;
      std::set<std::string> x, y;
      for (LocIdx l : a.occupied) x.insert(inst.locations[l].id);
      for (LocIdx l : b.occupied) y.insert(shuffled.locations[l].id);
      if (x != y) same = false;
    }
    Check(same, "row order does not change any activity's expansion");
    Eq(shuffled.exclusive_pairs.size(), inst.exclusive_pairs.size(),
       "row order does not change the buffer-conflict set");
    std::error_code ec; fs::remove_all(dir, ec);
  }

  // ---------------------------------------------------------------- bad input
  Group("input rejection");
  {
    const std::string dir = CopyInstance("cycle");
    auto lines = ReadLines(dir + "/08_ACTIVITY_DETAILS.csv");
    // Make two activities each other's predecessor.
    auto set_pred = [&](size_t row, const std::string& pred) {
      std::vector<std::string> f;
      std::string cur;
      for (char c : lines[row]) { if (c == ',') { f.push_back(cur); cur.clear(); } else cur.push_back(c); }
      f.push_back(cur);
      if (f.size() >= 8) f[7] = pred;
      std::string joined;
      for (size_t i = 0; i < f.size(); ++i) { if (i) joined += ","; joined += f[i]; }
      lines[row] = joined;
    };
    set_pred(1, "A002");
    set_pred(2, "A001");
    WriteLines(dir + "/08_ACTIVITY_DETAILS.csv", lines);
    Instance bad;
    std::vector<InputError> errs;
    Check(!LoadInstance(dir, &bad, &errs), "a predecessor cycle is rejected");
    bool named = false;
    for (const auto& e : errs) if (e.message.find("cycle") != std::string::npos) named = true;
    Check(named, "  ... and the error names the cycle");
    std::error_code ec; fs::remove_all(dir, ec);
  }
  {
    const std::string dir = CopyInstance("missing");
    std::error_code ec;
    fs::remove(dir + "/06_PARAMETERS.csv", ec);
    Instance bad;
    std::vector<InputError> errs;
    Check(!LoadInstance(dir, &bad, &errs), "a missing instance file is rejected");
    fs::remove_all(dir, ec);
  }
  {
    const std::string dir = CopyInstance("badref");
    auto lines = ReadLines(dir + "/08_ACTIVITY_DETAILS.csv");
    lines[1] = "A001,C999,Renewal,SEC:BET:S15_S16:EB,SEC:BET:S16_S17:EB,2,2027-05-24,,2";
    WriteLines(dir + "/08_ACTIVITY_DETAILS.csv", lines);
    Instance bad;
    std::vector<InputError> errs;
    Check(!LoadInstance(dir, &bad, &errs), "an activity referencing an unknown contract is rejected");
    bool has_field = false;
    for (const auto& e : errs) if (e.field == "contract_number" && e.row > 0) has_field = true;
    Check(has_field, "  ... and the error carries the row and field");
    std::error_code ec; fs::remove_all(dir, ec);
  }
  {
    // A crafted identifier must not be silently rewritten into something valid.
    const std::string dir = CopyInstance("inject");
    auto lines = ReadLines(dir + "/08_ACTIVITY_DETAILS.csv");
    lines[1] = "A001,C001,Renewal,../../etc/passwd,SEC:BET:S16_S17:EB,2,2027-05-24,,2";
    WriteLines(dir + "/08_ACTIVITY_DETAILS.csv", lines);
    Instance bad;
    std::vector<InputError> errs;
    Check(!LoadInstance(dir, &bad, &errs), "a path-traversal styled location id is rejected");
    std::error_code ec; fs::remove_all(dir, ec);
  }

  // ---------------------------------------------------------------- packing
  Group("slot packing");
  {
    // The two linear inequalities the solver uses must agree with what the
    // greedy packer actually achieves, for every small composition.
    auto min_slots = [](int pm, int pc, int c) {
      int s = pm + pc;
      const int spare = 3 * pc;
      if (c > spare) s += (c - spare + 3) / 4;
      return s;
    };
    bool agree = true;
    for (int pm = 0; pm <= 3; ++pm)
      for (int pc = 0; pc <= 4; ++pc)
        for (int c = 0; c <= 12; ++c) {
          const int g = min_slots(pm, pc, c);
          // Inequality form used in solver.cpp.
          if (!(g >= pm + pc)) agree = false;
          if (!(4 * g >= c + 4 * pm + pc)) agree = false;
          if (g > 0 && (g - 1 >= pm + pc) && (4 * (g - 1) >= c + 4 * pm + pc)) agree = false;  // not minimal
        }
    Check(agree, "the capacity inequalities exactly characterise the minimum slot count");
  }

  // ---------------------------------------------------------------- scoring
  Group("scoring");
  {
    // Weights are integers scaled by ten, so equality is exact.
    Eq(ContractWeight(1), 100, "priority 1 weight");
    Eq(ContractWeight(2), 10, "priority 2 weight");
    Eq(ContractWeight(3), 1, "priority 3 weight");
    Eq(ActivityNudgeTenths(1), 13, "activity priority 1 nudge (+0.3)");
    Eq(ActivityNudgeTenths(2), 12, "activity priority 2 nudge (+0.2)");
    Eq(ActivityNudgeTenths(3), 10, "activity priority 3 nudge (+0.0)");
    // The README's worked example, reproduced exactly. Values are in tenths,
    // so 91 reads as 9.1 and 7000 as 700.
    Eq(ContractWeight(3) * ActivityNudgeTenths(3) * 7, 70, "README: P3 contract / act3 / 7d = 7.0");
    Eq(ContractWeight(3) * ActivityNudgeTenths(1) * 7, 91, "README: P3 contract / act1 / 7d = 9.1");
    Eq(ContractWeight(2) * ActivityNudgeTenths(3) * 7, 700, "README: P2 contract / act3 / 7d = 70.0");
    Eq(ContractWeight(1) * ActivityNudgeTenths(3) * 7, 7000, "README: P1 contract / act3 / 7d = 700.0");
    // The nudge must never let a lower tier cross into a higher tier's band.
    Check(ContractWeight(2) * ActivityNudgeTenths(1) < ContractWeight(1) * ActivityNudgeTenths(3),
          "a P2 contract's ceiling stays below a P1 contract's floor");
    Check(ContractWeight(3) * ActivityNudgeTenths(1) < ContractWeight(2) * ActivityNudgeTenths(3),
          "a P3 contract's ceiling stays below a P2 contract's floor");
  }

  std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
  return g_fail == 0 ? 0 : 1;
}
