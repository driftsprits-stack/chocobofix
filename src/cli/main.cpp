// trackaccess - command line entry point.
//
// Subcommands:
//   solve     --data DIR --out DIR [--scenario A|B|C|all] [--seconds N] [--workers N]
//   validate  --data DIR --submission DIR
//
// Exit codes: 0 success, 1 usage/input error, 2 solve produced no plan,
// 3 validation found hard violations. Distinct codes let CI gate on them.
#include <atomic>
#include <cstdlib>
#include <map>
#include <set>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/instance.h"
#include "core/schedule.h"
#include "solver/solver.h"
#include "validator/validator.h"

namespace {

std::atomic<bool> g_cancel{false};
void OnSignal(int) { g_cancel.store(true); }

std::string Arg(const std::vector<std::string>& a, const std::string& key,
                const std::string& fallback = "") {
  for (size_t i = 0; i + 1 < a.size(); ++i)
    if (a[i] == key) return a[i + 1];
  return fallback;
}
bool Flag(const std::vector<std::string>& a, const std::string& key) {
  for (const auto& s : a) if (s == key) return true;
  return false;
}

int Usage() {
  std::cerr <<
      "usage:\n"
      "  trackaccess solve    --data DIR --out DIR [--scenario A|B|C|all]\n"
      "                       [--seconds N] [--workers N] [--seed N] [--log]\n"
      "  trackaccess validate --data DIR --submission DIR\n"
      "  trackaccess diagnose --data DIR [--scenario A|B|C] [--seconds N]\n"
      "  trackaccess explain  --data DIR --activity ID --week N [--scenario A|B|C]\n"
      "  trackaccess repair   --data DIR --out DIR --supply LOC@WEEK=N [--supply ...]\n"
      "  trackaccess compare  --data DIR --before DIR --after DIR\n";
  return 1;
}

bool LoadOrReport(const std::string& dir, ta::Instance* inst) {
  std::vector<ta::InputError> errors;
  if (ta::LoadInstance(dir, inst, &errors)) return true;
  std::cerr << "instance rejected (" << errors.size() << " problem"
            << (errors.size() == 1 ? "" : "s") << "):\n";
  for (size_t i = 0; i < errors.size() && i < 50; ++i)
    std::cerr << "  " << errors[i].Format() << "\n";
  if (errors.size() > 50) std::cerr << "  ... " << (errors.size() - 50) << " more\n";
  return false;
}

int RunSolve(const std::vector<std::string>& args) {
  const std::string data = Arg(args, "--data");
  const std::string out = Arg(args, "--out");
  if (data.empty() || out.empty()) return Usage();

  ta::Instance inst;
  if (!LoadOrReport(data, &inst)) return 1;

  std::vector<ta::Scenario> scenarios;
  const std::string which = Arg(args, "--scenario", "all");
  if (which == "all") scenarios = {ta::Scenario::kA, ta::Scenario::kB, ta::Scenario::kC};
  else if (auto s = ta::ParseScenario(which)) scenarios = {*s};
  else return Usage();

  ta::SolveOptions opts;
  opts.max_seconds = std::stod(Arg(args, "--seconds", "60"));
  opts.workers = std::stoi(Arg(args, "--workers", "8"));
  opts.random_seed = std::stoi(Arg(args, "--seed", "1"));
  opts.log_search = Flag(args, "--log");

  std::cout << "instance " << data << "\n  activities=" << inst.activities.size()
            << " contracts=" << inst.contracts.size()
            << " locations=" << inst.locations.size()
            << " weeks=" << inst.horizon_weeks
            << "\n  input_hash=" << inst.input_hash << "\n";

  int worst = 0;
  for (ta::Scenario sc : scenarios) {
    opts.scenario = sc;
    const std::string dir = out + "/" + std::string(ta::ToString(sc));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::cout << "\n=== scenario " << ta::ToString(sc) << " (budget "
              << opts.max_seconds << "s, " << opts.workers << " workers) ===\n";
    auto res = ta::Solve(inst, opts, &g_cancel,
                         [](long long obj, double el) {
                           std::cout << "  [" << el << "s] objective " << (obj / 10) << "."
                                     << (obj % 10) << "\n" << std::flush;
                         });
    std::cout << "  status: " << ta::ToString(res.status) << "  (" << res.solver_detail << ")\n";
    if (!res.message.empty()) std::cout << "  note: " << res.message << "\n";
    if (res.status != ta::SolveStatus::kOptimal && res.status != ta::SolveStatus::kFeasible) {
      worst = std::max(worst, 2);
      continue;
    }
    std::string err;
    if (!ta::ExportPlan(inst, res.plan, dir, &err)) {
      std::cerr << "  export failed: " << err << "\n";
      worst = std::max(worst, 2);
      continue;
    }
    // Re-read what was written and validate the files, not the objects that
    // produced them. A plan is only reported as exportable after this passes.
    ta::Plan reread;
    std::vector<ta::InputError> perrs;
    if (!ta::LoadPlan(inst, dir, &reread, &perrs)) {
      std::cerr << "  re-reading the exported files failed:\n";
      for (const auto& e : perrs) std::cerr << "    " << e.Format() << "\n";
      worst = std::max(worst, 2);
      continue;
    }
    const auto rep = ta::Validate(inst, reread);
    std::cout << "  exported to " << dir << "\n";
    std::cout << "  independent check: " << (rep.feasible ? "FEASIBLE" : "HARD VIOLATIONS")
              << " (" << rep.hard_violations.size() << ")\n";
    for (size_t i = 0; i < rep.hard_violations.size() && i < 10; ++i)
      std::cout << "    [" << rep.hard_violations[i].rule << "] "
                << rep.hard_violations[i].detail << "\n";
    std::cout << "  overrun_days=" << rep.soft_scores.overrun_days_total
              << " excess_nights=" << rep.soft_scores.excess_access_nights_total
              << " eclo=" << rep.soft_scores.eclo_nights_total
              << " objective=" << (rep.soft_scores.objective_tenths / 10) << "."
              << (rep.soft_scores.objective_tenths % 10);
    if (res.status == ta::SolveStatus::kOptimal) std::cout << "  (optimality proven)";
    else std::cout << "  (optimality NOT proven; best bound "
                   << (res.best_bound_tenths / 10) << "." << (res.best_bound_tenths % 10) << ")";
    std::cout << "\n";

    {
      const std::string jpath = dir + "/VALIDATION.json";
      FILE* f = std::fopen(jpath.c_str(), "wb");
      if (f) { const std::string j = rep.ToJson(inst); std::fwrite(j.data(), 1, j.size(), f); std::fclose(f); }
    }
    if (!rep.feasible) worst = std::max(worst, 3);
  }
  return worst;
}

// Tells an operator which rule group makes an instance infeasible, by lifting
// one group at a time and re-solving. A group whose removal restores
// feasibility is a binding cause; if none does, the causes are combined.
int RunDiagnose(const std::vector<std::string>& args) {
  const std::string data = Arg(args, "--data");
  if (data.empty()) return Usage();
  ta::Instance inst;
  if (!LoadOrReport(data, &inst)) return 1;

  ta::SolveOptions opts;
  opts.max_seconds = std::stod(Arg(args, "--seconds", "30"));
  opts.workers = std::stoi(Arg(args, "--workers", "8"));
  const std::string which = Arg(args, "--scenario", "A");
  if (auto sc = ta::ParseScenario(which)) opts.scenario = *sc; else return Usage();

  std::cout << "diagnosing scenario " << which << " on " << data << "\n";
  auto run = [&](unsigned relax, const char* label) {
    opts.relax = relax;
    auto r = ta::Solve(inst, opts, &g_cancel, nullptr);
    std::cout << "  " << std::string(label)
              << std::string(std::max<int>(1, 42 - (int)std::string(label).size()), ' ')
              << ta::ToString(r.status) << "\n";
    return r.status;
  };
  auto solved = [](ta::SolveStatus s) {
    return s == ta::SolveStatus::kOptimal || s == ta::SolveStatus::kFeasible;
  };
  const auto base = run(ta::kRelaxNone, "all rules enforced");
  if (base != ta::SolveStatus::kInfeasible) {
    std::cout << "\nThe instance is not infeasible under the full rule set; nothing to diagnose.\n";
    return 0;
  }
  std::vector<unsigned> groups = {ta::kRelaxBuffers, ta::kRelaxCapacity, ta::kRelaxWeekly,
                                  ta::kRelaxPrecedence, ta::kRelaxStartWeek};
  std::vector<const char*> binding, inconclusive;
  for (unsigned g : groups) {
    const std::string label = std::string("lift: ") + ta::RelaxName(g);
    const auto st = run(g, label.c_str());
    if (solved(st)) binding.push_back(ta::RelaxName(g));
    else if (st != ta::SolveStatus::kInfeasible) inconclusive.push_back(ta::RelaxName(g));
  }
  std::cout << "\n";
  if (!binding.empty()) {
    std::cout << "Lifting any one of these alone restores feasibility:\n";
    for (const char* b : binding) std::cout << "  - " << b << "\n";
  }
  if (!inconclusive.empty()) {
    std::cout << "Inconclusive within the budget (no plan found, and no proof of\n"
                 "infeasibility either) - re-run with a larger --seconds:\n";
    for (const char* b : inconclusive) std::cout << "  - " << b << "\n";
  }
  if (binding.empty() && inconclusive.empty())
    std::cout << "Every group remains infeasible on its own: the causes are combined,\n"
                 "or the horizon is too short for the workload and planned start weeks.\n";
  std::cout << "\nThis reports which modelled rule binds. It is not permission to breach it.\n";
  return 0;
}


// Parses "SEC:ALP:S01_S02:EB@12=1" into a supply override.
bool ParseOverride(const ta::Instance& inst, const std::string& spec, ta::SupplyOverride* out) {
  const auto at = spec.rfind('@');
  const auto eq = spec.rfind('=');
  if (at == std::string::npos || eq == std::string::npos || eq < at) return false;
  const std::string loc = spec.substr(0, at);
  auto it = inst.location_by_id.find(loc);
  if (it == inst.location_by_id.end()) return false;
  try {
    out->location = it->second;
    out->week = std::stoi(spec.substr(at + 1, eq - at - 1));
    out->supply = std::stoi(spec.substr(eq + 1));
  } catch (...) { return false; }
  return out->week >= 1 && out->week <= inst.horizon_weeks && out->supply >= 0;
}

void PrintDiff(const ta::Instance& inst, const ta::Plan& before, const ta::Plan& after) {
  std::map<std::string, std::set<ta::Week>> b, a;
  for (const auto& x : before.accesses) b[inst.activities[x.activity].id].insert(x.week);
  for (const auto& x : after.accesses) a[inst.activities[x.activity].id].insert(x.week);
  std::set<std::string> ids;
  for (const auto& [k, v] : b) ids.insert(k);
  for (const auto& [k, v] : a) ids.insert(k);
  int moved = 0;
  for (const auto& id : ids) {
    if (b[id] == a[id]) continue;
    ++moved;
    if (moved > 25) continue;
    std::cout << "    " << id << "  ";
    for (ta::Week w : b[id]) std::cout << w << " ";
    std::cout << " ->  ";
    for (ta::Week w : a[id]) std::cout << w << " ";
    std::cout << "\n";
  }
  if (moved > 25) std::cout << "    ... and " << (moved - 25) << " more\n";
  if (moved == 0) std::cout << "    (no activity changed week)\n";
  else std::cout << "    " << moved << " activities changed\n";
}

// "Why not earlier?" - tests whether one activity could take an access in a
// given week, and if not, which modelled rule stands in the way.
int RunExplain(const std::vector<std::string>& args) {
  const std::string data = Arg(args, "--data");
  const std::string act = Arg(args, "--activity");
  const std::string wk = Arg(args, "--week");
  if (data.empty() || act.empty() || wk.empty()) return Usage();

  ta::Instance inst;
  if (!LoadOrReport(data, &inst)) return 1;
  auto ait = inst.activity_by_id.find(act);
  if (ait == inst.activity_by_id.end()) {
    std::cerr << "no activity \"" << act << "\" in this instance\n";
    return 1;
  }
  const ta::ActIdx a = ait->second;
  const ta::Week w = std::stoi(wk);
  if (w < 1 || w > inst.horizon_weeks) {
    std::cerr << "week " << w << " is outside the 1.." << inst.horizon_weeks << " horizon\n";
    return 1;
  }

  ta::SolveOptions opts;
  opts.max_seconds = std::stod(Arg(args, "--seconds", "20"));
  opts.workers = std::stoi(Arg(args, "--workers", "8"));
  if (auto sc = ta::ParseScenario(Arg(args, "--scenario", "A"))) opts.scenario = *sc;
  else return Usage();

  const auto& A = inst.activities[a];
  std::cout << "Can " << A.id << " take an access in week " << w << "?  (scenario "
            << ta::ToString(opts.scenario) << ")\n\n";
  std::cout << "  what the instance itself says\n";
  std::cout << "    planned start week      " << A.earliest_week
            << "  (" << A.planned_start_date.ToIso() << ")\n";
  if (w < A.earliest_week)
    std::cout << "    -> week " << w << " is before its planned start week. Rule 2 forbids this"
                 " outright;\n       no search is needed.\n";
  if (A.predecessor != ta::kNoIndex)
    std::cout << "    predecessor             " << inst.activities[A.predecessor].id
              << " must finish in a strictly earlier week\n";
  std::cout << "    workload                " << A.total_accesses << " access-nights, one per week\n";
  std::cout << "    books                   " << A.occupied.size() << " locations\n";

  std::cout << "\n  baseline (unconstrained)\n";
  auto base = ta::Solve(inst, opts, &g_cancel, nullptr);
  if (base.status != ta::SolveStatus::kOptimal && base.status != ta::SolveStatus::kFeasible) {
    std::cout << "    " << ta::ToString(base.status)
              << " - cannot compare against a baseline that does not exist\n";
    return 2;
  }
  std::cout << "    objective " << (base.objective_tenths / 10) << "." << (base.objective_tenths % 10)
            << (base.status == ta::SolveStatus::kOptimal ? "  (proven optimal)" : "  (not proven)") << "\n";
  std::set<ta::Week> had;
  for (const auto& x : base.plan.accesses) if (x.activity == a) had.insert(x.week);
  std::cout << "    " << A.id << " currently takes weeks";
  for (ta::Week k : had) std::cout << " " << k;
  std::cout << "\n";

  std::cout << "\n  with the access in week " << w << " required\n";
  opts.require = {{a, w}};
  opts.baseline = &base.plan;
  opts.churn_weight_tenths = 1;   // tie-break toward keeping the rest of the plan
  auto test = ta::Solve(inst, opts, &g_cancel, nullptr);
  std::cout << "    " << ta::ToString(test.status) << "\n";

  if (test.status == ta::SolveStatus::kOptimal || test.status == ta::SolveStatus::kFeasible) {
    const long long d = test.objective_tenths - base.objective_tenths;
    std::cout << "\n  YES - it can. Objective " << (test.objective_tenths / 10) << "."
              << (test.objective_tenths % 10) << " ("
              << (d > 0 ? "+" : "") << (d / 10) << "." << (std::llabs(d) % 10)
              << " against the baseline), " << test.churn << " activity-weeks moved.\n";
    PrintDiff(inst, base.plan, test.plan);
    return 0;
  }
  if (test.status != ta::SolveStatus::kInfeasible) {
    std::cout << "\n  NOT ESTABLISHED. The search ran out of budget without finding a plan and\n"
                 "  without proving that none exists. This is not evidence of impossibility.\n"
                 "  Re-run with a larger --seconds.\n";
    return 2;
  }

  std::cout << "\n  NO - proven impossible under the modelled rules. Which rule binds:\n";
  ta::SolveOptions probe = opts;
  probe.baseline = nullptr;
  probe.churn_weight_tenths = 0;
  for (unsigned g : {ta::kRelaxBuffers, ta::kRelaxCapacity, ta::kRelaxWeekly,
                     ta::kRelaxPrecedence, ta::kRelaxStartWeek}) {
    probe.relax = g;
    auto r = ta::Solve(inst, probe, &g_cancel, nullptr);
    const bool unblocks = r.status == ta::SolveStatus::kOptimal || r.status == ta::SolveStatus::kFeasible;
    std::cout << "    " << (unblocks ? "BINDING  " : "         ") << ta::RelaxName(g)
              << (unblocks ? "" : "  (still impossible without it)") << "\n";
  }
  // Name the activities that physically cannot share that week with it.
  std::vector<std::string> rivals;
  for (const auto& [i, j] : inst.exclusive_pairs) {
    if (i == a) rivals.push_back(inst.activities[j].id);
    else if (j == a) rivals.push_back(inst.activities[i].id);
  }
  if (!rivals.empty()) {
    std::cout << "\n  " << A.id << " can never share a week with " << rivals.size()
              << " other activities\n  (closure or buffer overlap): ";
    for (size_t k = 0; k < rivals.size() && k < 12; ++k) std::cout << rivals[k] << " ";
    if (rivals.size() > 12) std::cout << "...";
    std::cout << "\n";
  }
  return 0;
}

// Disruption repair: apply a change to location supply in the input's own terms,
// re-plan, and report what had to move.
int RunRepair(const std::vector<std::string>& args) {
  const std::string data = Arg(args, "--data");
  const std::string out = Arg(args, "--out");
  if (data.empty() || out.empty()) return Usage();

  ta::Instance inst;
  if (!LoadOrReport(data, &inst)) return 1;

  ta::SolveOptions opts;
  opts.max_seconds = std::stod(Arg(args, "--seconds", "60"));
  opts.workers = std::stoi(Arg(args, "--workers", "8"));
  if (auto sc = ta::ParseScenario(Arg(args, "--scenario", "A"))) opts.scenario = *sc;
  else return Usage();

  std::vector<ta::SupplyOverride> overrides;
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] != "--supply") continue;
    ta::SupplyOverride o;
    if (!ParseOverride(inst, args[i + 1], &o)) {
      std::cerr << "cannot read \"" << args[i + 1]
                << "\"; expected LOCATION_ID@WEEK=SUPPLY, e.g. SEC:ALP:S01_S02:EB@12=1\n";
      return 1;
    }
    overrides.push_back(o);
  }
  if (overrides.empty()) {
    std::cerr << "no disruption given; pass at least one --supply LOCATION_ID@WEEK=SUPPLY\n";
    return 1;
  }

  std::cout << "Disruption repair, scenario " << ta::ToString(opts.scenario) << "\n\n  before\n";
  auto base = ta::Solve(inst, opts, &g_cancel, nullptr);
  if (base.status != ta::SolveStatus::kOptimal && base.status != ta::SolveStatus::kFeasible) {
    std::cout << "    " << ta::ToString(base.status) << " - nothing to repair from\n";
    return 2;
  }
  std::cout << "    objective " << (base.objective_tenths / 10) << "." << (base.objective_tenths % 10)
            << "  overrun " << base.score.overrun_days_total << "d\n";

  std::cout << "\n  disruption\n";
  for (const auto& o : overrides)
    std::cout << "    " << inst.locations[o.location].id << " week " << o.week
              << ": supply " << inst.locations[o.location].supply_capacity
              << " -> " << o.supply << "\n";

  opts.supply_overrides = overrides;
  opts.baseline = &base.plan;
  // Small relative to the scenario's own penalties, so it only ever breaks ties
  // between plans the competition objective scores equally.
  opts.churn_weight_tenths = std::stoi(Arg(args, "--churn-weight", "1"));

  std::cout << "\n  after\n";
  auto rep = ta::Solve(inst, opts, &g_cancel, nullptr);
  std::cout << "    " << ta::ToString(rep.status) << "\n";
  if (rep.status != ta::SolveStatus::kOptimal && rep.status != ta::SolveStatus::kFeasible) {
    std::cout << "\n  The disrupted network cannot carry this workload under scenario "
              << ta::ToString(opts.scenario) << ".\n  The previous plan is unchanged on disk and is"
                 " now OUTDATED for these conditions:\n  it is not safe to dispatch against the"
                 " reduced supply.\n";
    return 2;
  }
  const long long d = rep.objective_tenths - base.objective_tenths;
  std::cout << "    objective " << (rep.objective_tenths / 10) << "." << (rep.objective_tenths % 10)
            << "  (" << (d > 0 ? "+" : "") << (d / 10) << "." << (std::llabs(d) % 10) << ")"
            << "  overrun " << rep.score.overrun_days_total << "d"
            << "  churn " << rep.churn << " activity-weeks\n\n  what moved\n";
  PrintDiff(inst, base.plan, rep.plan);

  std::error_code ec;
  std::filesystem::create_directories(out, ec);
  std::string err;
  if (!ta::ExportPlan(inst, rep.plan, out, &err)) {
    std::cerr << "  export failed: " << err << "\n";
    return 2;
  }
  ta::Plan reread;
  std::vector<ta::InputError> perrs;
  if (!ta::LoadPlan(inst, out, &reread, &perrs)) { std::cerr << "  re-read failed\n"; return 2; }
  const auto v = ta::Validate(inst, reread);
  std::cout << "\n  repaired plan exported to " << out << "; independent check: "
            << (v.feasible ? "FEASIBLE" : "HARD VIOLATIONS") << "\n";
  return v.feasible ? 0 : 3;
}

// Before/after comparison of two already-exported submissions.
int RunCompare(const std::vector<std::string>& args) {
  const std::string data = Arg(args, "--data");
  const std::string before = Arg(args, "--before");
  const std::string after = Arg(args, "--after");
  if (data.empty() || before.empty() || after.empty()) return Usage();
  ta::Instance inst;
  if (!LoadOrReport(data, &inst)) return 1;
  ta::Plan pb, pa;
  std::vector<ta::InputError> e1, e2;
  if (!ta::LoadPlan(inst, before, &pb, &e1)) { std::cerr << "cannot read " << before << "\n"; return 1; }
  if (!ta::LoadPlan(inst, after, &pa, &e2)) { std::cerr << "cannot read " << after << "\n"; return 1; }
  const auto vb = ta::Validate(inst, pb);
  const auto va = ta::Validate(inst, pa);
  std::cout << "before  scenario " << ta::ToString(pb.scenario)
            << "  " << (vb.feasible ? "feasible" : "INFEASIBLE")
            << "  objective " << (vb.soft_scores.objective_tenths / 10) << "."
            << (vb.soft_scores.objective_tenths % 10)
            << "  overrun " << vb.soft_scores.overrun_days_total << "d\n";
  std::cout << "after   scenario " << ta::ToString(pa.scenario)
            << "  " << (va.feasible ? "feasible" : "INFEASIBLE")
            << "  objective " << (va.soft_scores.objective_tenths / 10) << "."
            << (va.soft_scores.objective_tenths % 10)
            << "  overrun " << va.soft_scores.overrun_days_total << "d\n\n";
  PrintDiff(inst, pb, pa);
  return 0;
}

int RunValidate(const std::vector<std::string>& args) {
  const std::string data = Arg(args, "--data");
  const std::string sub = Arg(args, "--submission");
  if (data.empty() || sub.empty()) return Usage();

  ta::Instance inst;
  if (!LoadOrReport(data, &inst)) return 1;

  ta::Plan plan;
  std::vector<ta::InputError> errors;
  if (!ta::LoadPlan(inst, sub, &plan, &errors)) {
    std::cerr << "submission rejected (" << errors.size() << " problems):\n";
    for (size_t i = 0; i < errors.size() && i < 50; ++i)
      std::cerr << "  " << errors[i].Format() << "\n";
    return 1;
  }
  const auto rep = ta::Validate(inst, plan);
  std::cout << rep.ToJson(inst) << "\n";
  return rep.feasible ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  const std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) return Usage();
  if (args[0] == "solve") return RunSolve(args);
  if (args[0] == "validate") return RunValidate(args);
  if (args[0] == "diagnose") return RunDiagnose(args);
  if (args[0] == "explain") return RunExplain(args);
  if (args[0] == "repair") return RunRepair(args);
  if (args[0] == "compare") return RunCompare(args);
  return Usage();
}
