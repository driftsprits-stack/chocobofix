#pragma once

#include <atomic>
#include <utility>
#include <vector>
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

// A disruption expressed in the input's own semantics: the supply available at
// one location in one week is overridden. Used for "urgent maintenance has taken
// two of the four nights at S01-S02 in week 12".
struct SupplyOverride {
  LocIdx location = kNoIndex;
  Week week = 0;
  int supply = 0;
};

struct SolveOptions {
  Scenario scenario = Scenario::kA;
  unsigned relax = kRelaxNone;
  double max_seconds = 60.0;
  int workers = 8;
  int random_seed = 1;

  // Hypotheses under test. `require` forces an access, `forbid` removes one.
  std::vector<std::pair<ActIdx, Week>> require;
  std::vector<std::pair<ActIdx, Week>> forbid;

  // Disruption: per location-week supply replacing LOCATION_SUPPLY.
  std::vector<SupplyOverride> supply_overrides;

  // Fallback mode. The scenario's POLICY constraint (B's fixed completion dates,
  // A's zero-excess supply, C's one-excess allowance) becomes a heavily penalised
  // soft constraint instead of a hard one. Every physical safety rule - closures,
  // buffers, legal mixes, precedence, planned start weeks, workfronts - stays
  // hard. A plan produced this way is NOT conforming and must never be presented
  // as submission-ready; it exists so an operator can see how far out of policy
  // the instance is, rather than being handed nothing at all.
  bool soft_scenario_policy = false;

  // Use the literal reading of rule 6 (see Instance::exclusive_pairs_strict).
  // Costs score and may be unschedulable; provided so the cost of that reading
  // can be measured rather than assumed.
  bool strict_buffers = false;

  // Repair mode. When `baseline` is set, the search additionally prefers to keep
  // its assignments. This preference is NEVER part of the competition score:
  // SolveResult::score is always the scenario's own objective, recomputed from
  // the plan, and `churn` is reported separately.
  const Plan* baseline = nullptr;
  int churn_weight_tenths = 0;
  // Scenario C allows at most one excess access-night per location-week; B is
  // unbounded (scored, not failed); A permits none.
  bool log_search = false;
};

struct SolveResult {
  SolveStatus status = SolveStatus::kInternalError;
  Plan plan;
  Score score;
  double wall_seconds = 0;
  long long objective_tenths = 0;    // the scenario's competition objective
  long long search_objective_tenths = 0;  // what CP-SAT minimised (may include churn)
  long long best_bound_tenths = 0;   // meaningful only when a solution exists
  int churn = 0;                     // activity-weeks differing from the baseline
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
