#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "core/schedule.h"

namespace ta {

// Outcomes are kept distinct because they demand different operator responses.
// In particular a search that ran out of time without a solution is NOT the same
// claim as a proof that no legal plan exists.
enum class SolveStatus {
  kOptimal,        // complete plan, optimality proven
  kFeasible,       // complete plan found, optimality NOT proven
  kInfeasible,     // proven: no plan satisfies the hard rules
  kTimeout,        // budget exhausted with no complete plan
  kCancelled,      // operator stopped the run
  kInvalidInput,   // instance rejected before search
  kInternalError
};

std::string_view ToString(SolveStatus s);

// Constraint groups that can be individually lifted, to tell an operator WHICH
// rule makes an instance infeasible. Used only by the diagnose path; a normal
// solve always runs with every group enforced.
enum Relax : unsigned {
  kRelaxNone       = 0,
  kRelaxBuffers    = 1u << 0,   // closure / buffer exclusivity
  kRelaxCapacity   = 1u << 1,   // per location-week possession slots
  kRelaxWeekly     = 1u << 2,   // contract nights x workfronts
  kRelaxPrecedence = 1u << 3,   // predecessor finish-to-start
  kRelaxStartWeek  = 1u << 4,   // planned start weeks
};
const char* RelaxName(unsigned bit);

struct SolveOptions {
  Scenario scenario = Scenario::kA;
  unsigned relax = kRelaxNone;
  double max_seconds = 60.0;
  int workers = 8;
  int random_seed = 1;
  // Scenario C allows at most one excess access-night per location-week; B is
  // unbounded (scored, not failed); A permits none.
  bool log_search = false;
};

struct SolveResult {
  SolveStatus status = SolveStatus::kInternalError;
  Plan plan;
  Score score;
  double wall_seconds = 0;
  long long objective_tenths = 0;
  long long best_bound_tenths = 0;   // meaningful only when a solution exists
  std::string solver_detail;         // CP-SAT status name and counters
  std::string message;
};

// Progress for the UI: called on each improving solution. Must be cheap.
using ProgressFn = std::function<void(long long objective_tenths, double elapsed_seconds)>;

// `cancel` is polled during search; setting it stops the solve and returns the
// best complete plan found so far, if any.
SolveResult Solve(const Instance& inst, const SolveOptions& opts,
                  std::atomic<bool>* cancel = nullptr, ProgressFn progress = nullptr);

}  // namespace ta
