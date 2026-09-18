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
};

struct Plan {
  Scenario scenario = Scenario::kA;
  std::vector<Access> accesses;    // sorted by (activity id, week) on export

  // Set by ComputeOccupancy. Key is (location, week); value maps each present
  // activity to its 1-based slot index, rendered as "b1".."bN" on export.
  std::map<std::pair<LocIdx, Week>, std::map<ActIdx, int>> slots;
  std::map<std::pair<LocIdx, Week>, int> slots_used;
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

// Reads back an exported plan. Used by the validator so that what is checked is
// the bytes on disk, not the in-memory objects that produced them.
bool LoadPlan(const Instance& inst, const std::string& dir, Plan* out,
              std::vector<InputError>* errors);

}  // namespace ta
