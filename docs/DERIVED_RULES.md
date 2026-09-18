# Derived rule semantics and ambiguity register

Upstream commit: `966c976005db2e3e40a691cff268fdb8f396a5df` (PS1, fetched 2026-09-18).

**Method.** `PS1_README.md` leaves several rules under-specified. The pack ships
`03_submission_sample/`, described upstream as "a feasible, 0-hard-violation
submission against `01_data/`". Each candidate interpretation below was executed
against that sample; an interpretation that reports the sample as *infeasible* is
refuted. Scripts: `tools/derive/` (re-runnable).

**The official reference validator is NOT in the upstream repository.** The brief
references `python3 -m trackaccess expand`; no such module, script or archive
exists anywhere in the repo tree (verified over the full recursive tree listing).
Everything below is therefore *our reconstruction*, corroborated only by the
sample. **No claim in this project may describe our checker's verdict as an
official-validator result.**

---

## R1. Span expansion — RESOLVED, exact

An activity spans `start_location_id` → `end_location_id` (both `SEC:<line>:<a>_<b>:<bound>`).
Occupied locations = every tunnel sector from the first to the last in `03_SECTORS.seq`
order, **plus every platform sector from the first sector's `from_station` through the
last sector's `to_station`, endpoints inclusive**.

Verified: reproduces all 192 activity-weeks of the sample occupancy exactly, 0 mismatches.

Rejected: platform-exclusive-of-endpoints (would drop 2 platforms per activity).

## R2. `supply_capacity` = access-nights per location-week — RESOLVED

`supply_capacity` counts **distinct `co_share_group` values** permitted at that
location in that week, i.e. how many separate possession-nights the location offers.

| Interpretation | Sample violations |
| --- | --- |
| distinct `co_share_group` per location-week ≤ capacity | **0 / 702** |
| count of activity rows per location-week ≤ capacity | 69 |

The second reading is refuted. §2.1's "4 nights in Week 4, per LOCATION_SUPPLY"
corroborates the first.

## R3. Legal mix applies *within* one slot — RESOLVED

Within a single `(location_id, week, co_share_group)`: one `PM` alone, or ≤1 `PC`
plus ≤3 `C`, or ≤4 `C`. 0 illegal slots in 751. Observed compositions include
`4×C`, `PC+3×C`, `PM` alone — consistent and at the stated limits.

Encoded linearly (no explicit slot variables needed) as, per location-week:
- `groups ≥ n_PM + n_PC`
- `4·groups ≥ n_C + 4·n_PM + n_PC`

These two are exactly equivalent to "a legal packing into `groups` slots exists".

## R4. One access per activity per week — RESOLVED

Max accesses for any single activity in one week in the sample = 1. Consistent with
§2.4 r10's "since an activity gets at most one access-night per week". Treated as hard.

## R5. Completion date and overrun — RESOLVED, exact

`simulated_completion_date = horizon_start + (last_access_week − 1)×7 + 6`
(the Sunday of the last access week). Matches 14/14 contracts exactly; the
"+0" (Monday) variant matches 0/14. Not clamped to the planned date — contracts
finishing early report their true earlier date.

`overrun_days = max(0, simulated − planned)`, verified consistent for all 14 rows.
Always a multiple of 7, so overrun is **linear in the last access week** — encoded
directly in CP-SAT.

## R6. Closure and buffer — RESOLVED against the sample, with a residual caveat

Two distinct exclusions, which the brief describes in different language and which
must not be conflated:

**R6a — closure.** An activity's closure is its worksite, plus for `Live` the
mirrored opposite bound (cutting traction power takes both bounds together) and,
at the interchange only, the other line's `H01_H02` tunnel and its `H01`/`H02`
platforms. §2.4 r4: "no external activity may enter it that night" — a closure
excludes **every** other activity. The cross-line set is exactly the one the brief
names and is not itself widened by any buffer.

**R6b — buffer.** The closure grown by `up_to_buffer_sectors`, counted in whole
sectors along each (line, bound) chain the worksite touches. §2.4 r4 scopes its
effect explicitly: a buffer "pushes the next **`Live`/`Non-Live(Consist)`** work on
that bound". A buffer therefore constrains only other buffer-carrying work;
`Non-live (Others)`, which carries no buffer, is not pushed by anyone else's.

Conflict test between two activities in the same week:
- if `occupied(A) ∩ occupied(B) ≠ ∅` → **no conflict**. Their relationship at the
  shared location is already settled by `co_share_group`: equal ⇒ one possession
  (§2.4 r6 exempts them); different ⇒ provably different nights.
- else conflict if `closure(A) ∩ occupied(B) ≠ ∅` or the reverse;
- else, **only when both carry a buffer**, conflict if
  `buffer_zone(A) ∩ occupied(B) ≠ ∅` or the reverse.

| Variant | Sample violations |
| --- | --- |
| **closure binds all, buffer binds only buffered pairs** (adopted) | **0** |
| single buffered zone binding all pairs | 0 |
| buffer counted in chain steps rather than sectors | 28 |
| exemption requires a shared *slot label* rather than a shared location | 5 |
| no exemption at all | 105 |

The first two both accept the sample, but the adopted rule is strictly the more
permissive of them and is the one the brief's wording actually describes, so it is
preferred: it admits schedules the stricter reading would reject, without admitting
anything the sample shows to be legal. On the public instance it reduces the
never-same-week pair count from 62 to 58.

**Caveat (unverifiable).** Where two activities share *no* location, nothing in the
output schema establishes whether they run on the same night — `access_night` is a
per-contract accounting index, not a network-wide night id. We take the
conservative branch and treat them as potentially concurrent. If the official
validator is *less* strict here we lose some score; no rule consistent with a
feasible sample could be stricter. **Our solver is never less strict than the
evidence allows.**

## R7. Scoring — per-activity, not per-contract — PARTIALLY RESOLVED

`priority_weighted_score = Σ_activities contract_weight(tier) × (1 + nudge) × overrun_days(activity)`
with `contract_weight` 100/10/1 for contract tier 1/2/3 and `nudge` +0.3/+0.2/+0.0
for `activity_priority` 1/2/3. An activity's `overrun_days` is measured against its
**contract's** `planned_completion_date`.

Corroboration: the README's specimen report has `overrun_days_total: 126` but
`priority_overrun` summing to 378 — the two cannot both be per-contract, and 378 is
only reachable by per-activity accumulation. `RESULTS.csv` stays per-contract.

**Unverified:** the specimen report is illustrative, not the sample's own output, so
the exact aggregation cannot be confirmed. Computed in integers scaled ×10
(`weight × (10 + nudge10) × days`) to avoid float comparison; reported /10.

## R8. Open items — NOT resolved

- **O1.** Whether `earliness_days_total` carries any score weight. Assumed **no** (it
  appears in `soft_scores` but in none of §2.5's three formulas). Not optimised.
- **O2.** Scenario B "excess access-nights above nominal supply" — assumed identical
  in meaning to C's per-location-week excess, i.e. `Σ max(0, groups_used − capacity)`.
  No sample exists for B or C (the shipped sample is Scenario A), so B/C scoring is
  uncorroborated by any official artefact.
- **O3.** Whether a `PM` activity may co-share with *itself* across locations. Moot
  here: `PM` is alone in its slot by R3 either way.
- **O4.** Whether the validator requires `access_seq` to be contiguous from 1 and
  ordered by week. We emit it that way regardless (1..n ascending by week).
