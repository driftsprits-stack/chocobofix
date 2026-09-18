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

  // ------------------------------------------------- interpretation counterexamples
  // A hand-built micro network (tests/data/micro) whose only purpose is to make
  // the competing readings of rule 6 disagree on a NAMED pair, so the choice is
  // pinned by a test rather than by prose. See docs/DERIVED_RULES.md R6.
  Group("rule 6 counterexamples");
  {
    Instance micro;
    std::vector<InputError> merr;
    Check(LoadInstance("tests/data/micro", &micro, &merr), "the micro instance loads");
    auto idx = [&](const char* id) { return micro.activity_by_id.at(id); };
    auto pair_in = [&](const std::vector<std::pair<ActIdx, ActIdx>>& v, const char* a, const char* b) {
      ActIdx i = idx(a), j = idx(b);
      if (i > j) std::swap(i, j);
      return std::find(v.begin(), v.end(), std::make_pair(i, j)) != v.end();
    };

    // THE DIVERGING CASE. AX (Consist, S01_S02..S02_S03 EB) and AY (Consist,
    // S03_S04 EB) both book PLAT:ALP:S03:EB. AX's buffer reaches
    // SEC:ALP:S03_S04:EB, which is AY's worksite.
    //   adopted reading - they share a location, so the pair is settled by
    //     co_share_group and they MAY share a week;
    //   literal reading - rule 6's "buffers apply normally between them" makes
    //     it a breach, so they may NOT.
    // If this assertion ever flips, the project has silently changed its
    // interpretation of the brief.
    Check(!pair_in(micro.exclusive_pairs, "AX", "AY"),
          "adopted reading: AX and AY may share a week (they share PLAT:ALP:S03:EB)");
    Check(pair_in(micro.exclusive_pairs_strict, "AX", "AY"),
          "literal reading: AX and AY may NOT share a week (AX's buffer reaches AY)");

    // A second diverging pair in the opposite direction: AM's buffer reaches
    // into AX's worksite, and they share three locations.
    Check(!pair_in(micro.exclusive_pairs, "AX", "AM"), "adopted: AX and AM may share a week");
    Check(pair_in(micro.exclusive_pairs_strict, "AX", "AM"), "literal: AX and AM may not");

    // WHERE THE READINGS AGREE - these must hold under either, so they guard the
    // parts of the rule that are not in dispute.
    Check(pair_in(micro.exclusive_pairs, "AX", "AL") &&
          pair_in(micro.exclusive_pairs_strict, "AX", "AL"),
          "both readings: a Live closure mirrored onto the other bound excludes AX");
    Check(!pair_in(micro.exclusive_pairs, "AX", "AZ") &&
          !pair_in(micro.exclusive_pairs_strict, "AX", "AZ"),
          "both readings: AX and AZ never interact, so neither excludes them");

    // R6b: a buffer pushes only other buffer-carrying work. AN is
    // Non-live (Others) and carries none, so AY's buffer must not push it.
    // Under a reading where buffers bind on everyone, this pair would appear.
    Check(!pair_in(micro.exclusive_pairs, "AY", "AN"),
          "a buffer does not push Non-live (Others) work (R6b)");
    Check(!pair_in(micro.exclusive_pairs_strict, "AY", "AN"),
          "  ... and that holds under the literal reading too");
    Check(micro.activities[idx("AN")].buffer_zone.size() ==
          micro.activities[idx("AN")].occupied.size(),
          "Non-live (Others) has no zone beyond its own worksite");

    // The literal reading is strictly stronger: every adopted exclusion is also
    // a literal one. If this ever fails the two sets have drifted apart.
    bool superset = true;
    for (const auto& pr : micro.exclusive_pairs)
      if (std::find(micro.exclusive_pairs_strict.begin(), micro.exclusive_pairs_strict.end(), pr) ==
          micro.exclusive_pairs_strict.end()) superset = false;
    Check(superset, "the literal exclusion set contains the adopted one");
    Check(micro.exclusive_pairs_strict.size() > micro.exclusive_pairs.size(),
          "and is strictly larger on this instance, so the readings really do differ");

    // Live specifics, which no reading disputes.
    const auto& AL = micro.activities[idx("AL")];
    bool crosses_bound = false;
    for (LocIdx l : AL.closure) if (micro.locations[l].bound == Bound::kEB) crosses_bound = true;
    Check(crosses_bound, "a Live closure reaches the opposite bound");
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

  // ------------------------------------------------- reviewed risk regressions
  // One test per risk raised in the implementation brief's section 9. Each
  // failed before the fix; none may regress silently.
  Group("risk: horizon clamping");
  {
    // A planned start after the horizon used to be clamped backwards into the
    // last week, quietly letting the work start earlier than the data allows.
    const std::string dir = CopyInstance("beyond");
    auto lines = ReadLines(dir + "/08_ACTIVITY_DETAILS.csv");
    // horizon is 30 weeks from 2027-01-04; 2028-06-05 is far outside it.
    lines[1] = "A001,C001,Renewal,SEC:BET:S15_S16:EB,SEC:BET:S16_S17:EB,2,2028-06-05,,2";
    WriteLines(dir + "/08_ACTIVITY_DETAILS.csv", lines);
    Instance bad;
    std::vector<InputError> errs;
    Check(!LoadInstance(dir, &bad, &errs),
          "a planned start after the horizon is rejected, not clamped backwards");
    bool explains = false;
    for (const auto& e : errs)
      if (e.message.find("after the") != std::string::npos &&
          e.field == "planned_start_date") explains = true;
    Check(explains, "  ... and the error says the activity could never be scheduled");
    std::error_code ec; fs::remove_all(dir, ec);
  }
  {
    // A date before the horizon is a different case: it just means "as early as
    // the horizon allows", and must still load.
    const std::string dir = CopyInstance("before");
    auto lines = ReadLines(dir + "/08_ACTIVITY_DETAILS.csv");
    lines[1] = "A001,C001,Renewal,SEC:BET:S15_S16:EB,SEC:BET:S16_S17:EB,2,2026-05-01,,2";
    WriteLines(dir + "/08_ACTIVITY_DETAILS.csv", lines);
    Instance early;
    std::vector<InputError> errs;
    Check(LoadInstance(dir, &early, &errs), "a planned start before the horizon still loads");
    if (!errs.empty()) for (auto& e : errs) std::cout << "    " << e.Format() << "\n";
    Eq(early.activities[early.activity_by_id.at("A001")].earliest_week, 1,
       "  ... and becomes week 1");
    std::error_code ec; fs::remove_all(dir, ec);
  }
  Eq(inst.WeekOf(*Date::Parse("2028-06-05")) > inst.horizon_weeks, true,
     "WeekOf itself no longer clamps: a date past the horizon returns a later week");
  Check(!inst.WeekInHorizon(inst.WeekOf(*Date::Parse("2028-06-05"))),
        "  ... and WeekInHorizon reports it as outside");

  Group("risk: access_seq and RESULTS are checked, not trusted");
  {
    Plan p = sample;
    p.accesses[0].access_seq = 99;
    const auto r = Validate(inst, p);
    Check(CountRule(r, "schema") > 0, "a wrong access_seq is rejected");
  }
  {
    Plan p = sample;
    // Two accesses of one activity given the same sequence number.
    ActIdx victim = p.accesses[0].activity;
    int n = 0;
    for (auto& a : p.accesses) if (a.activity == victim && n++ < 2) a.access_seq = 1;
    const auto r = Validate(inst, p);
    Check(CountRule(r, "schema") > 0, "a duplicated access_seq is rejected");
  }
  {
    Plan p = sample;
    Check(!p.results_rows.empty(), "the sample's RESULTS rows were read back");
    p.results_rows[0].overrun_days += 7;          // claim more overrun than the schedule shows
    const auto r = Validate(inst, p);
    Check(CountRule(r, "schema") > 0, "an overrun_days that disagrees with the schedule is rejected");
  }
  {
    Plan p = sample;
    p.results_rows[0].simulated_completion_date =
        Date{p.results_rows[0].simulated_completion_date.days + 7};
    const auto r = Validate(inst, p);
    Check(CountRule(r, "schema") > 0,
          "a simulated_completion_date that disagrees with the schedule is rejected");
  }
  {
    Plan p = sample;
    p.results_rows.erase(p.results_rows.begin());
    const auto r = Validate(inst, p);
    Check(CountRule(r, "schema") > 0, "a contract missing from RESULTS.csv is rejected");
  }
  {
    Plan p = sample;
    p.results_rows.push_back(p.results_rows[0]);
    const auto r = Validate(inst, p);
    Check(CountRule(r, "schema") > 0, "a contract listed twice in RESULTS.csv is rejected");
  }
  {
    Plan p = sample;
    p.results_rows[0].scenario = "C";             // file says A
    const auto r = Validate(inst, p);
    Check(CountRule(r, "schema") > 0, "a RESULTS row naming a different scenario is rejected");
  }

  Group("risk: buffer zones that merely overlap");
  {
    // "Buffers never overlap" read literally rejects the shipped sample, so it
    // is enforced only under the strict reading. Both halves are asserted so
    // neither can drift: the default must accept, strict must reject.
    ValidationOptions lenient, overlap;
    overlap.no_zone_overlap = true;
    const auto a = Validate(inst, sample, lenient);
    const auto b = Validate(inst, sample, overlap);
    Check(a.feasible, "the sample passes under the adopted reading");
    Check(!b.feasible, "the sample fails once merely-touching zones are forbidden");
    Check(CountRule(b, "buffer_overlap") > 0, "  ... on overlapping exclusion zones specifically");
    Check(CountRule(a, "buffer_overlap") == 0, "  ... which the adopted reading does not raise");
    // The three readings are strictly nested, so a plan clean under a stricter
    // one is clean under a looser one.
    Check(inst.exclusive_pairs.size() <= inst.exclusive_pairs_strict.size(),
          "the strict exclusion set contains the adopted one");
    Check(inst.exclusive_pairs_strict.size() <= inst.exclusive_pairs_no_overlap.size(),
          "and the no-overlap set contains the strict one");
  }

  Group("risk: provenance travels with a repaired plan");
  {
    // A plan solved against reduced supply must be checked against that reduced
    // supply. Exporting and re-reading must carry the disruption with it.
    Plan p = sample;
    const LocIdx loc = inst.location_by_id.at("SEC:BET:H01_H02:EB");
    p.provenance.supply_overrides.emplace_back("SEC:BET:H01_H02:EB", 13, 0);
    const std::string dir = (fs::temp_directory_path() / "ta_prov").string();
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    std::string err;
    Check(ExportPlan(inst, p, dir, &err), "a plan with provenance exports: " + err);
    Check(fs::exists(dir + "/PROVENANCE.json"), "PROVENANCE.json is written beside the outputs");

    Plan back;
    std::vector<InputError> perr;
    Check(LoadPlan(inst, dir, &back, &perr), "and is read back");
    Eq(back.provenance.supply_overrides.size(), size_t(1), "the override survives the round trip");
    Eq(EffectiveSupply(inst, back, loc, 13), 0, "the reduced supply is what a check now uses");
    Eq(EffectiveSupply(inst, back, loc, 14), inst.locations[loc].supply_capacity,
       "other weeks keep their nominal supply");
    // With supply cut to zero in week 13, work there must now be over capacity.
    const auto r = Validate(inst, back);
    Check(CountRule(r, "capacity") > 0,
          "work at a location whose supply the disruption removed is now a capacity breach");
    fs::remove_all(dir, ec);
  }

  Group("risk: the checker does not reuse the solver's conflict set");
  {
    // Structural, and the reason the checker can catch a solver mistake at all:
    // src/validator/validator.cpp must not mention exclusive_pairs. If it ever
    // does again, the two stop being independent and this test says so.
    std::string src;
    Check(ReadFile("src/validator/validator.cpp", &src), "the checker source is readable");
    // Look for actual use (`inst.exclusive_pairs`), not the identifier, which
    // appears in the comment explaining why it is not used.
    Check(src.find("inst.exclusive_pairs") == std::string::npos,
          "the checker never consumes the solver's precomputed conflict pairs");
    Check(src.find("plan.slots") != std::string::npos,
          "  ... it works from the emitted occupancy and its co_share_group values");
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
