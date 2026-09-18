// Toolchain gate: proves the pinned OR-Tools CP-SAT C++ API builds, links and
// solves on this machine before any domain code depends on it.
#include <cstdlib>
#include <iostream>

#include "ortools/base/version.h"
#include "ortools/sat/cp_model.h"

int main() {
  using operations_research::sat::CpModelBuilder;
  using operations_research::sat::CpSolverStatus;

  std::cout << "OR-Tools version: " << operations_research::OrToolsVersionString()
            << "\n";

  // Tiny scheduling-shaped problem: three jobs, two slots, at most two per slot,
  // job c must run strictly after job a.
  CpModelBuilder model;
  const auto a = model.NewIntVar({0, 2}).WithName("a");
  const auto b = model.NewIntVar({0, 2}).WithName("b");
  const auto c = model.NewIntVar({0, 2}).WithName("c");
  model.AddGreaterThan(c, a);
  model.AddAllDifferent({a, b, c});
  model.Minimize(a + b + c);

  const auto response = operations_research::sat::Solve(model.Build());
  std::cout << "status: " << CpSolverStatus_Name(response.status()) << "\n";
  if (response.status() != CpSolverStatus::OPTIMAL) {
    std::cerr << "FAIL: expected OPTIMAL\n";
    return EXIT_FAILURE;
  }
  std::cout << "a=" << operations_research::sat::SolutionIntegerValue(response, a)
            << " b=" << operations_research::sat::SolutionIntegerValue(response, b)
            << " c=" << operations_research::sat::SolutionIntegerValue(response, c)
            << "\nobjective=" << response.objective_value() << "\n";
  std::cout << "SMOKE OK\n";
  return EXIT_SUCCESS;
}
