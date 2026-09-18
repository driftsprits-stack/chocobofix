// Domain model for PS1 railway track access optimisation.
//
// Identifiers that appear in competition output (activity ids, location ids,
// contract numbers, enum spellings) are canonical: they are carried through as
// the exact strings read from the instance and are never rewritten by the UI
// language or by any presentation layer.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ta {

// ---------------------------------------------------------------------------
// Scalar wrappers. Weeks are 1-based over the horizon read from 06_PARAMETERS.
// ---------------------------------------------------------------------------
using Week = int;            // 1 .. horizon_weeks
using LocIdx = int;          // index into Instance::locations
using ActIdx = int;          // index into Instance::activities
using ConIdx = int;          // index into Instance::contracts

inline constexpr int kNoIndex = -1;

// A civil date, stored as days since epoch for cheap arithmetic. The CSV form is
// always ISO-8601 (YYYY-MM-DD) in both input and output.
struct Date {
  std::int64_t days = 0;

  static std::optional<Date> Parse(std::string_view iso);
  std::string ToIso() const;

  friend bool operator==(Date a, Date b) { return a.days == b.days; }
  friend bool operator<(Date a, Date b) { return a.days < b.days; }
  friend std::int64_t operator-(Date a, Date b) { return a.days - b.days; }
};

// ---------------------------------------------------------------------------
// Enumerations. Parsed strictly: an unrecognised spelling is an input error
// rather than a silent default, because a silent default would change which
// safety buffer applies.
// ---------------------------------------------------------------------------
enum class AccessType { kPM, kPC, kC };

// Buffer size and mirroring are data-driven from 05_BUFFER_LOCATION.csv; this
// enum only identifies the row.
enum class Nature { kLive, kNonLiveConsist, kNonLiveOthers };

enum class LocationKind { kTunnelSector, kPlatformSector };

enum class Bound { kEB, kWB };

enum class Scenario { kA, kB, kC };

std::string_view ToString(AccessType t);
std::string_view ToString(Nature n);
std::string_view ToString(Scenario s);
std::optional<AccessType> ParseAccessType(std::string_view s);
std::optional<Nature> ParseNature(std::string_view s);
std::optional<Scenario> ParseScenario(std::string_view s);
inline Bound Opposite(Bound b) { return b == Bound::kEB ? Bound::kWB : Bound::kEB; }

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
struct Location {
  std::string id;              // canonical, e.g. "SEC:ALP:S02_S03:EB"
  LocationKind kind{};
  std::string line;            // "ALP" / "BET"
  Bound bound{};
  int supply_capacity = 0;     // access-nights available per week (see R2)

  // Position on the per-(line,bound) alternating chain
  //   PLAT s0, SEC s0_s1, PLAT s1, SEC s1_s2, ...
  // Buffer growth walks this chain. -1 until the topology is built.
  int chain_index = kNoIndex;
  int sector_ordinal = kNoIndex;  // 0-based count of sectors at/before this slot
};

struct BufferRule {
  int up_to_buffer_sectors = 0;
  bool opposite_bound_required = false;
};

// ---------------------------------------------------------------------------
// Demand
// ---------------------------------------------------------------------------
struct Contract {
  std::string number;                 // canonical, e.g. "C001"
  std::string description;
  std::string activity_type;          // "Renewal" / "Construction" (free text)
  Nature nature{};
  int priority = 3;                   // 1 high .. 3 low
  Date contract_completion_date{};
  Date planned_completion_date{};
  int number_of_workfronts = 1;
  AccessType access_type{};
  int max_access_per_week = 1;

  // Derived: the week whose Sunday is planned_completion_date. An activity
  // finishing in a later week overruns.
  Week planned_completion_week = 0;
};

struct Activity {
  std::string id;                     // canonical, e.g. "A012"
  ConIdx contract = kNoIndex;
  std::string activity_type;
  std::string start_location_id;
  std::string end_location_id;
  int total_accesses = 0;             // whole access-nights of work required
  Date planned_start_date{};
  ActIdx predecessor = kNoIndex;
  int activity_priority = 3;

  Week earliest_week = 1;             // from planned_start_date

  // Topology expansion (R1): every location booked on any night this activity
  // works. Constant across weeks - an activity's worksite does not move.
  std::vector<LocIdx> occupied;
  // Closure (R6a): the worksite itself, plus - for Live only - the mirrored
  // opposite bound and the cross-line interchange tunnel/platforms. A closure
  // excludes EVERY other activity: no external work may enter a closed sector.
  std::vector<LocIdx> closure;
  // Buffer zone (R6b): the closure grown by the nature's buffer in sectors.
  // Per the brief, a buffer "pushes the next Live/Non-live (Consist) work" -
  // it constrains only other buffer-carrying work, not Non-live (Others).
  std::vector<LocIdx> buffer_zone;
  bool carries_buffer = false;
};

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------
struct Instance {
  Date horizon_start{};
  int horizon_weeks = 0;

  std::vector<Location> locations;
  std::vector<Contract> contracts;
  std::vector<Activity> activities;
  std::unordered_map<Nature, BufferRule> buffers;

  std::unordered_map<std::string, LocIdx> location_by_id;
  std::unordered_map<std::string, ConIdx> contract_by_number;
  std::unordered_map<std::string, ActIdx> activity_by_id;

  // Pairs (a,b), a<b, that may never share a week: their closure zones reach
  // each other's worksite and they share no location, so nothing in the output
  // schema could establish that they run on different nights (R6).
  std::vector<std::pair<ActIdx, ActIdx>> exclusive_pairs;

  // Activities occupying each location, for the per-location-week capacity rows.
  std::vector<std::vector<ActIdx>> activities_at;

  // SHA-256 over the eight input files, in canonical filename order. Binds a
  // plan to the exact bytes it was produced from.
  std::string input_hash;

  Week WeekOf(Date d) const;          // 1-based; clamped to [1, horizon_weeks]
  Date SundayOfWeek(Week w) const;    // horizon_start + (w-1)*7 + 6
  const Contract& ContractOf(const Activity& a) const { return contracts[a.contract]; }
};

// Scoring weights. Held as integers scaled by 10 so that score comparison and
// the CP-SAT objective are exact: no floating-point equality anywhere.
inline int ContractWeight(int priority) {
  switch (priority) {
    case 1: return 100;
    case 2: return 10;
    default: return 1;
  }
}
// +0.3 / +0.2 / +0.0 expressed in tenths on top of a base of 10.
inline int ActivityNudgeTenths(int activity_priority) {
  switch (activity_priority) {
    case 1: return 13;
    case 2: return 12;
    default: return 10;
  }
}

inline constexpr int kExcessAccessPenalty = 7;
inline constexpr int kEcloPenalty = 5;
inline constexpr int kEcloYieldTenths = 15;   // an ECLO night yields 1.5 units
inline constexpr int kStdYieldTenths = 10;

}  // namespace ta
