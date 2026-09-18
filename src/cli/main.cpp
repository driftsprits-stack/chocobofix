// trackaccess - command line entry point.
//
// Subcommands:
//   solve     --data DIR --out DIR [--scenario A|B|C|all] [--seconds N] [--workers N]
//   validate  --data DIR --submission DIR
//
// Exit codes: 0 success, 1 usage/input error, 2 solve produced no plan,
// 3 validation found hard violations. Distinct codes let CI gate on them.
#include <atomic>
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
      "  trackaccess diagnose --data DIR [--scenario A|B|C] [--seconds N]\n";
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
  return Usage();
}
