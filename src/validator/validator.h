// Independent conformance checker.
//
// This deliberately does NOT reuse the solver's constraint construction. It
// consumes a Plan parsed back from the emitted CSV files and re-derives every
// hard rule from the instance, so a modelling mistake in the solver cannot make
// the checker agree with it. Only schema parsing and topology expansion are
// shared, since both must read the same network.
//
// This is OUR checker. It is not, and must never be described as, the official
// reference validator, which is absent from the upstream repository.
#pragma once

#include <string>
#include <vector>

#include "core/schedule.h"

namespace ta {

struct Violation {
  std::string rule;      // tag: workload, planned_start, precedence, closure,
                         // capacity, mix, allocation, workfront, eclo,
                         // planned_date, occupancy, schema
  std::string severity;  // "hard"
  std::string detail;    // human-readable pinpoint: activity, week, location
};

// Which reading of rule 6 to check against. The default is the one the shipped
// sample is consistent with; `strict_buffers` additionally enforces the literal
// readings, which the sample refutes (docs/DERIVED_RULES.md R6c and R6d). A plan
// produced with --strict-buffers is checked strictly, because that is the
// stronger guarantee its author asked for.
struct ValidationOptions {
  bool strict_buffers = false;
  // "Buffers never overlap" at face value. Off even under strict_buffers,
  // because no schedule for the public instance can satisfy it.
  bool no_zone_overlap = false;
};

struct ValidationReport {
  Scenario scenario = Scenario::kA;
  bool feasible = false;
  std::vector<Violation> hard_violations;
  Score soft_scores;
  std::vector<std::string> capacity_hotspots;
  std::string checker_version = "trackaccess-check/0.2";
  bool strict_buffers = false;
  // Carried from the plan's provenance so a report says what it was checked
  // against, not merely what the instance nominally supplies.
  int supply_overrides = 0;
  bool fallback = false;

  std::string ToJson(const Instance& inst) const;
};

ValidationReport Validate(const Instance& inst, const Plan& plan,
                          ValidationOptions opts = {});

}  // namespace ta
