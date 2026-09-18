#include "solver/solver.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <map>
#include <set>
#include <set>
#include <sstream>

#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/util/time_limit.h"

namespace ta {

using operations_research::sat::BoolVar;
using operations_research::sat::CpModelBuilder;
using operations_research::sat::CpSolverResponse;
using operations_research::sat::CpSolverStatus;
using operations_research::sat::IntVar;
using operations_research::sat::LinearExpr;

const char* RelaxName(unsigned bit) {
  switch (bit) {
    case kRelaxBuffers: return "closure/buffer exclusivity";
    case kRelaxCapacity: return "location possession capacity";
    case kRelaxWeekly: return "contract weekly nights x workfronts";
    case kRelaxPrecedence: return "predecessor precedence";
    case kRelaxStartWeek: return "planned start weeks";
    default: return "none";
  }
}

std::string_view ToString(SolveStatus s) {
  switch (s) {
    case SolveStatus::kOptimal: return "optimal";
    case SolveStatus::kFeasible: return "feasible";
    case SolveStatus::kInfeasible: return "infeasible";
    case SolveStatus::kTimeout: return "timeout_no_solution";
    case SolveStatus::kCancelled: return "cancelled";
    case SolveStatus::kInvalidInput: return "invalid_input";
    default: return "internal_error";
  }
}

SolveResult Solve(const Instance& inst, const SolveOptions& opts,
                  std::atomic<bool>* cancel, ProgressFn progress) {
  const auto t0 = std::chrono::steady_clock::now();
  SolveResult out;
  out.plan.scenario = opts.scenario;

  const int H = inst.horizon_weeks;
  const int na = static_cast<int>(inst.activities.size());
  if (na == 0) {
    out.status = SolveStatus::kInvalidInput;
    out.message = "instance contains no activities";
    return out;
  }

  CpModelBuilder m;

  // x[a][w] : activity a takes an access-night in week w. At most one per week
  // is structural (R4) - there is simply no variable for a second one.
  // e[a][w] : that night is an Early Closure / Late Opening night.
  std::vector<std::vector<BoolVar>> x(na), e(na);
  for (int a = 0; a < na; ++a) {
    x[a].resize(H + 1);
    e[a].resize(H + 1);
    for (int w = 1; w <= H; ++w) {
      x[a][w] = m.NewBoolVar();
      e[a][w] = m.NewBoolVar();
      // Rule 2: nothing before the planned start week.
      if (w < inst.activities[a].earliest_week && !(opts.relax & kRelaxStartWeek))
        m.FixVariable(x[a][w], false);
      // ECLO implies an access on that night; Scenario A forbids ECLO outright.
      if (opts.scenario == Scenario::kA) m.FixVariable(e[a][w], false);
      else m.AddLessOrEqual(e[a][w], x[a][w]);
    }
  }

  // Rule 1, workload conservation, in exact tenths: a standard night yields 10
  // units, an ECLO night 15. No floating point enters the model.
  for (int a = 0; a < na; ++a) {
    LinearExpr yield;
    for (int w = 1; w <= H; ++w) yield += kStdYieldTenths * x[a][w] + 5 * e[a][w];
    const int need = inst.activities[a].total_accesses * kStdYieldTenths;
    m.AddGreaterOrEqual(yield, need).WithName("workload_" + inst.activities[a].id);
    // No strictly redundant night. Rule 1 only requires the yield to REACH the
    // workload, but an access that could be dropped while still meeting it wins
    // nothing and occupies a possession slot another contract could use. The
    // smallest night yields 10 tenths, so capping the total at need+9 forbids
    // exactly those accesses and no legal combination of standard/ECLO nights.
    m.AddLessOrEqual(yield, need + kStdYieldTenths - 1);
  }

  // First and last access week per activity, used by precedence and scoring.
  std::vector<IntVar> first(na), last(na);
  for (int a = 0; a < na; ++a) {
    first[a] = m.NewIntVar({1, H + 1});
    last[a] = m.NewIntVar({0, H});
    std::vector<LinearExpr> lo, hi;
    for (int w = 1; w <= H; ++w) {
      hi.push_back(LinearExpr(w * x[a][w]));                       // w if taken, else 0
      lo.push_back(LinearExpr((H + 1)) - (H + 1 - w) * x[a][w]);   // w if taken, else H+1
    }
    m.AddMaxEquality(last[a], hi);
    m.AddMinEquality(first[a], lo);
  }

  // Rule 3: finish-to-start with zero lag, measured in whole weeks. The
  // successor's first night must fall in a strictly later week than the
  // predecessor's last - not merely later within the same week.
  for (int a = 0; a < na; ++a) {
    const ActIdx p = inst.activities[a].predecessor;
    if (p != kNoIndex && !(opts.relax & kRelaxPrecedence))
      m.AddGreaterOrEqual(first[a], last[p] + 1);
  }

  // Rules 7 and 8 combined: a contract+type can field at most
  // (granted nights x workfronts) distinct activities in a week. Satisfying this
  // is exactly what makes a legal access_night assignment exist, which
  // AssignAccessNights then constructs.
  {
    std::map<std::pair<ConIdx, int>, std::vector<int>> by_contract;
    for (int a = 0; a < na; ++a) {
      const auto& act = inst.activities[a];
      by_contract[{act.contract, static_cast<int>(inst.ContractOf(act).access_type)}].push_back(a);
    }
    for (const auto& [key, acts] : by_contract) {
      const auto& c = inst.contracts[key.first];
      const int cap = c.max_access_per_week * c.number_of_workfronts;
      if (opts.relax & kRelaxWeekly) continue;
      for (int w = 1; w <= H; ++w) {
        LinearExpr n;
        for (int a : acts) n += x[a][w];
        m.AddLessOrEqual(n, cap);
      }
    }
  }

  // Rule 5 / R2: per location-week the activities present must pack into at most
  // `supply_capacity` possession slots. Two linear inequalities characterise the
  // minimum slot count exactly (see docs/DERIVED_RULES.md R3):
  //     slots >= n_PM + n_PC
  //   4*slots >= n_C + 4*n_PM + n_PC
  int max_excess = opts.scenario == Scenario::kA ? 0
                 : opts.scenario == Scenario::kC ? 1 : 4;
  // In fallback mode the supply ceiling stops being a wall. It is still paid for
  // at the scenario's own rate, plus the fallback surcharge below.
  if (opts.soft_scenario_policy) max_excess = std::max(max_excess, 8);
  std::map<std::pair<LocIdx, Week>, IntVar> excess;
  // A disruption replaces the nominal supply at specific location-weeks.
  std::map<std::pair<LocIdx, Week>, int> supply_at;
  for (const auto& o : opts.supply_overrides) supply_at[{o.location, o.week}] = o.supply;
  if (!(opts.relax & kRelaxCapacity))
  for (LocIdx l = 0; l < static_cast<LocIdx>(inst.locations.size()); ++l) {
    const auto& acts = inst.activities_at[l];
    if (acts.empty()) continue;
    for (int w = 1; w <= H; ++w) {
      auto ov = supply_at.find({l, w});
      const int cap = ov == supply_at.end() ? inst.locations[l].supply_capacity : ov->second;
      LinearExpr masters, weighted;
      for (ActIdx a : acts) {
        switch (inst.ContractOf(inst.activities[a]).access_type) {
          case AccessType::kPM: masters += x[a][w]; weighted += 4 * x[a][w]; break;
          case AccessType::kPC: masters += x[a][w]; weighted += 1 * x[a][w]; break;
          default:                                  weighted += 1 * x[a][w]; break;
        }
      }
      if (max_excess == 0) {
        m.AddLessOrEqual(masters, cap);
        m.AddLessOrEqual(weighted, 4 * cap);
      } else {
        IntVar ex = m.NewIntVar({0, max_excess});
        m.AddLessOrEqual(masters, cap + ex);
        m.AddLessOrEqual(weighted, 4 * (cap + ex));
        excess[{l, w}] = ex;
      }
    }
  }

  // Hypotheses under test: a forced or removed access for one activity-week.
  for (const auto& [a, w] : opts.require)
    if (a >= 0 && a < na && w >= 1 && w <= H) m.FixVariable(x[a][w], true);
  for (const auto& [a, w] : opts.forbid)
    if (a >= 0 && a < na && w >= 1 && w <= H) m.FixVariable(x[a][w], false);

  // Rule 4 / R6: pairs whose closure zones reach each other's worksite and which
  // share no location can never be proven to run on different nights, so they may
  // not share a week at all.
  if (!(opts.relax & kRelaxBuffers))
    for (const auto& [i, j] : inst.exclusive_pairs)
      for (int w = 1; w <= H; ++w) m.AddAtMostOne({x[i][w], x[j][w]});

  // Rule 10: under Scenario C every ECLO night affecting a line must fall in one
  // continuous span of at most two calendar weeks, chosen per line. A cross-line
  // Live activity must satisfy both lines' windows at once.
  if (opts.scenario == Scenario::kC) {
    std::map<std::string, IntVar> window_start;
    for (const auto& L : inst.locations)
      if (!window_start.count(L.line)) window_start[L.line] = m.NewIntVar({1, H});
    for (int a = 0; a < na; ++a) {
      std::set<std::string> lines;
      for (LocIdx l : inst.activities[a].closure) lines.insert(inst.locations[l].line);
      for (int w = 1; w <= H; ++w)
        for (const auto& ln : lines) {
          m.AddGreaterOrEqual(w, window_start.at(ln)).OnlyEnforceIf(e[a][w]);
          m.AddLessOrEqual(w, window_start.at(ln) + 1).OnlyEnforceIf(e[a][w]);
        }
    }
  }

  // --- objective -----------------------------------------------------------
  // overrun_days(a) = max(0, Sunday(last[a]) - planned_completion_date(contract))
  //                 = max(0, 7*last[a] - P), with P a constant per contract.
  // Exactly linear in last[a], so no date arithmetic enters the search.
  LinearExpr objective;
  std::vector<IntVar> overrun(na);
  const bool scores_overrun = opts.scenario != Scenario::kB;
  for (int a = 0; a < na; ++a) {
    const auto& c = inst.ContractOf(inst.activities[a]);
    const long long P = c.planned_completion_date.days - inst.horizon_start.days + 1;
    const int max_over = std::max<int>(0, static_cast<int>(7LL * H - P));
    overrun[a] = m.NewIntVar({0, max_over});
    m.AddGreaterOrEqual(overrun[a], LinearExpr(7 * last[a]) - static_cast<int>(P));
    if (opts.scenario == Scenario::kB) {
      // Planned dates are rigid in B: a feasible submission has zero overrun.
      if (!opts.soft_scenario_policy) {
        m.AddLessOrEqual(LinearExpr(7 * last[a]), static_cast<int>(P));
      } else {
        // Out of policy, but priced far above any legitimate lever so the search
        // still exhausts ECLO and extra nights before accepting a single day late.
        objective += 1000 * overrun[a];
      }
    } else {
      objective += static_cast<int>(ContractWeight(c.priority) *
                                    ActivityNudgeTenths(inst.activities[a].activity_priority)) *
                   overrun[a];
    }
  }
  if (!excess.empty()) {
    LinearExpr ex_total;
    for (auto& [k, v] : excess) ex_total += v;
    int rate = 10 * kExcessAccessPenalty;
    // A and C cap excess as policy; when that cap is lifted, each night beyond it
    // costs far more than any in-policy alternative.
    if (opts.soft_scenario_policy && opts.scenario != Scenario::kB) rate += 10000;
    objective += rate * ex_total;
  }
  if (opts.scenario != Scenario::kA) {
    LinearExpr eclo_total;
    for (int a = 0; a < na; ++a)
      for (int w = 1; w <= H; ++w) eclo_total += e[a][w];
    objective += (10 * kEcloPenalty) * eclo_total;
  }
  (void)scores_overrun;

  // Repair preference, kept strictly separate from the competition objective
  // above: it only breaks ties between plans the scenario scores equally, and
  // the score reported afterwards is recomputed from the plan without it.
  LinearExpr churn_expr;
  std::vector<std::vector<bool>> base(na, std::vector<bool>(H + 1, false));
  if (opts.baseline) {
    for (const auto& acc : opts.baseline->accesses)
      if (acc.activity >= 0 && acc.activity < na && acc.week >= 1 && acc.week <= H)
        base[acc.activity][acc.week] = true;
    for (int a = 0; a < na; ++a)
      for (int w = 1; w <= H; ++w)
        churn_expr += base[a][w] ? (LinearExpr(1) - x[a][w]) : LinearExpr(x[a][w]);
    if (opts.churn_weight_tenths > 0) objective += opts.churn_weight_tenths * churn_expr;
  }
  m.Minimize(objective);

  // --- search --------------------------------------------------------------
  operations_research::sat::Model model;
  operations_research::sat::SatParameters params;
  params.set_max_time_in_seconds(opts.max_seconds);
  params.set_num_workers(opts.workers);
  params.set_random_seed(opts.random_seed);
  params.set_log_search_progress(opts.log_search);
  model.Add(operations_research::sat::NewSatParameters(params));

  if (progress) {
    model.Add(operations_research::sat::NewFeasibleSolutionObserver(
        [&](const CpSolverResponse& r) {
          const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
          progress(static_cast<long long>(r.objective_value()), el);
        }));
  }
  // CP-SAT may itself write to the boolean it is given as an external limit, so
  // it never receives the operator's cancellation flag directly. A watcher
  // mirrors one into the other; `cancel` then means "the operator stopped this"
  // and nothing else, which keeps `cancelled` distinct from `timeout`.
  std::atomic<bool> stop_flag{false};
  std::atomic<bool> finished{false};
  std::thread watcher;
  if (cancel) {
    model.GetOrCreate<operations_research::TimeLimit>()->RegisterExternalBooleanAsLimit(&stop_flag);
    watcher = std::thread([&] {
      while (!finished.load(std::memory_order_relaxed)) {
        if (cancel->load(std::memory_order_relaxed)) { stop_flag.store(true); return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
  }

  const CpSolverResponse r = operations_research::sat::SolveCpModel(m.Build(), &model);
  finished.store(true);
  if (watcher.joinable()) watcher.join();
  out.wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  std::ostringstream detail;
  detail << "cp_sat_status=" << CpSolverStatus_Name(r.status())
         << " branches=" << r.num_branches() << " conflicts=" << r.num_conflicts()
         << " walltime=" << r.wall_time();
  out.solver_detail = detail.str();

  const bool has_solution = r.status() == CpSolverStatus::OPTIMAL ||
                            r.status() == CpSolverStatus::FEASIBLE;
  if (r.status() == CpSolverStatus::INFEASIBLE) {
    out.status = SolveStatus::kInfeasible;
    out.message = "no plan can satisfy the hard rules of this scenario for this instance";
    return out;
  }
  if (!has_solution) {
    out.status = (cancel && cancel->load()) ? SolveStatus::kCancelled : SolveStatus::kTimeout;
    out.message = "no complete plan was found within the run budget; "
                  "this is not a proof that none exists";
    return out;
  }

  // --- extract -------------------------------------------------------------
  for (int a = 0; a < na; ++a)
    for (int w = 1; w <= H; ++w)
      if (operations_research::sat::SolutionBooleanValue(r, x[a][w])) {
        Access acc;
        acc.activity = a;
        acc.week = w;
        acc.eclo = operations_research::sat::SolutionBooleanValue(r, e[a][w]);
        out.plan.accesses.push_back(acc);
      }
  std::string err;
  if (!AssignAccessNights(inst, &out.plan, &err)) {
    out.status = SolveStatus::kInternalError;
    out.message = "solution violates the weekly allocation model: " + err;
    return out;
  }
  ComputeOccupancy(inst, &out.plan);
  out.score = ComputeScore(inst, out.plan);
  // The reported objective is the scenario's own, recomputed from the extracted
  // plan - never CP-SAT's value, which in repair mode also carries the churn term.
  out.objective_tenths = out.score.objective_tenths;
  out.search_objective_tenths = static_cast<long long>(r.objective_value());
  if (opts.baseline) {
    std::set<std::pair<ActIdx, Week>> now, was;
    for (const auto& acc : out.plan.accesses) now.insert({acc.activity, acc.week});
    for (const auto& acc : opts.baseline->accesses) was.insert({acc.activity, acc.week});
    int diff = 0;
    for (const auto& k : now) if (!was.count(k)) ++diff;
    for (const auto& k : was) if (!now.count(k)) ++diff;
    out.churn = diff;
  }
  out.best_bound_tenths = static_cast<long long>(r.best_objective_bound());
  out.status = (r.status() == CpSolverStatus::OPTIMAL) ? SolveStatus::kOptimal
                                                       : SolveStatus::kFeasible;
  if (cancel && cancel->load() && out.status != SolveStatus::kOptimal)
    out.message = "run stopped by operator; best complete plan found so far is returned";
  return out;
}

}  // namespace ta
