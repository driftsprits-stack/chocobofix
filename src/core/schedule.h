// Plan representation, co-share slot packing, scoring, and competition export.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/instance.h"
#include "core/model.h"

namespace ta {

// One access-night granted to one activity. At most one per activity per week (R4).
struct Access {
  ActIdx activity = kNoIndex;
  Week week = 0;
  bool eclo = false;
  int access_night = 0;   // 1..max_access_per_week, local to (contract, type, week)
  int access_seq = 0;     // as written in the file; checked, not assumed
};

// A row of RESULTS.csv exactly as emitted, so the validator can recompute the
// figures and compare rather than trusting them.
struct ResultRow {
  std::string scenario;
  std::string contract_number;
  Date simulated_completion_date{};
  int overrun_days = 0;
};

// What a plan was produced against, beyond the instance itself. A repaired plan
// was solved against changed supply, and checking it against nominal supply
// would be checking a different question.
struct Provenance {
  std::vector<std::tuple<std::string, Week, int>> supply_overrides;  // location, week, supply
  bool strict_buffers = false;
  bool fallback = false;
  // True when this plan was produced by a search whose result is reproducible:
  // a single worker and a fixed seed. Multi-worker CP-SAT races its workers
  // against wall-clock time and may return a different optimum of equal value on
  // every run, which would make a published artefact hash unreproducible.
  bool deterministic = true;
  int workers = 1;
  int random_seed = 1;
  std::string solver_detail;
};

struct Plan {
  Scenario scenario = Scenario::kA;
  std::vector<Access> accesses;    // sorted by (activity id, week) on export

  // Set by ComputeOccupancy. Key is (location, week); value maps each present
  // activity to its 1-based slot index, rendered as "b1".."bN" on export.
  std::map<std::pair<LocIdx, Week>, std::map<ActIdx, int>> slots;
  std::map<std::pair<LocIdx, Week>, int> slots_used;

  std::vector<ResultRow> results_rows;   // as read back from RESULTS.csv
  Provenance provenance;
};

// Packs the activities present at each location-week into the fewest legal slots
// (R3: one PM alone, or <=1 PC with <=3 C, or <=4 C). Deterministic. The solver
// constrains slot demand to capacity, so this never needs to overflow; when it
// does (a hand-edited plan) the extra slots are reported as excess, not hidden.
void ComputeOccupancy(const Instance& inst, Plan* plan);

// Distributes each (contract, type, week)'s activities across that contract's
// granted nights, at most `number_of_workfronts` activities per night (R7/R8).
// Returns false if the week's load cannot fit, which the solver prevents.
bool AssignAccessNights(const Instance& inst, Plan* plan, std::string* error);

// --- scoring ---------------------------------------------------------------
struct Score {
  int overrun_days_total = 0;         // per contract, as reported in RESULTS.csv
  int contracts_overrunning = 0;
  int earliness_days_total = 0;
  int excess_access_nights_total = 0;
  int eclo_nights_total = 0;
  std::map<int, int> priority_overrun;      // contract tier -> raw overrun-days
  long long priority_weighted_tenths = 0;   // exact; divide by 10 to display
  long long objective_tenths = 0;           // scenario's combined penalty, x10
  int nights_scheduled = 0;
};

// Last access week per contract; 0 when the contract has no scheduled access.
std::vector<Week> ContractLastWeek(const Instance& inst, const Plan& plan);
std::vector<Week> ActivityLastWeek(const Instance& inst, const Plan& plan);

Score ComputeScore(const Instance& inst, const Plan& plan);

// Writes SCHEDULE_ACCESS.csv, SCHEDULE_OCCUPANCY.csv and RESULTS.csv into `dir`.
// One scenario per directory: RESULTS.csv must never mix scenarios.
bool ExportPlan(const Instance& inst, const Plan& plan, const std::string& dir, std::string* error);

// Effective supply at a location-week, honouring any provenance override.
int EffectiveSupply(const Instance& inst, const Plan& plan, LocIdx loc, Week week);

// Reads back an exported plan. Used by the validator so that what is checked is
// the bytes on disk, not the in-memory objects that produced them.
bool LoadPlan(const Instance& inst, const std::string& dir, Plan* out,
              std::vector<InputError>* errors);

}  // namespace ta
