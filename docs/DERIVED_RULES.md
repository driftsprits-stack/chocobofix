# Derived rule semantics and ambiguity register

Upstream commit: `966c976005db2e3e40a691cff268fdb8f396a5df` (PS1, fetched 2026-09-18).

**Method.** `PS1_README.md` leaves several rules under-specified and, in one
place, self-contradictory. The pack ships `03_submission_sample/`, described
upstream as "a feasible, 0-hard-violation submission against `01_data/`". Each
candidate interpretation below was executed against that sample; an interpretation
that reports the sample as *infeasible* contradicts the brief's own claim about
its own artefact. Scripts: `tools/derive/` (re-runnable).

**The official reference validator is NOT in the upstream repository.** The brief
references `python3 -m trackaccess expand`; no such module, script or archive
exists anywhere in the repo tree (verified over the full recursive tree listing).
Everything below is therefore *our reconstruction*. **No claim in this project may
describe our checker's verdict as an official-validator result.**

## What the evidence here can and cannot establish

This section exists because it is easy to overstate what we know.

**Two checkers agreeing does not establish an interpretation.** This project has a
C++ checker (`src/validator/`) and an independent Python one
(`tools/derive/crosscheck.py`), written separately and sharing no code. Their
agreement is evidence that **the rules are implemented faithfully in both** — it
catches coding mistakes. It is *not* evidence that the rules are the right ones,
because both encode the same reading. Wherever this document says two
implementations agree, read it as implementation fidelity and nothing more.

**Sample acceptance is supporting evidence, not proof.** That our reading accepts
the shipped sample is consistent with being right; it does not exclude other
readings that also accept it, and it rests on the organisers' claim that the
sample is 0-violation, which we cannot verify without their validator. Where a
reading is *refuted* by the sample the evidence is stronger — a reading that calls
the organisers' own artefact infeasible is contradicted by the brief — but even
that assumes the sample is current.

**Status labels used below:**

| Label | Meaning |
| --- | --- |
| **Settled** | The sample discriminates decisively, and no competing reading survives it. |
| **Adopted, contested** | The brief is self-contradictory. A reading is chosen, the alternative is named, and the cost of being wrong is measured. |
| **Open** | Not resolved. No artefact discriminates. |

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

## R6. Closure and buffer — two sub-rules, one **settled**, one **adopted, contested**

### R6a — closure. Settled.

> "Occupied night maintenance work closes a sector; no external activity may enter
> it that night. ... `Live` mirrors closure to opposite bound, and — only for
> `Live` — also crosses onto the other line's `H01_H02` tunnel sector/platforms at
> the interchange. Non-Live work never crosses lines there"

An activity's closure is its worksite, plus for `Live` the mirrored opposite bound
and, at the interchange only, the other line's `H01_H02` tunnel and its `H01`/`H02`
platforms. A closure excludes **every** other activity. The cross-line set is
exactly the one the brief names and is not itself widened by any buffer.

Pinned by `tests/test_core.cpp` (`AX/AL` excluded under both readings; a Live
closure reaches the opposite bound).

### R6b — whom a buffer pushes. Settled.

> "Buffers never overlap — a `Live`/`Non-Live(Consist)` work whose buffer reaches,
> say, `S02` **pushes the next `Live`/`Non-Live(Consist)` work** on that bound to
> start no earlier than `S03`."

The sentence names what a buffer pushes: other `Live`/`Non-Live(Consist)` work.
`Non-live (Others)`, which carries no buffer, is not pushed by anyone else's.
Counterexample test: `AY` (Consist) and `AN` (Others) occupy adjacent ground and
`AY`'s buffer reaches `AN`'s worksite, yet the pair is not excluded. A reading in
which buffers bind on everyone would fail that test.

Buffer distance is counted in whole **sectors** along the (line, bound) chain.
Counting raw chain steps instead makes the sample infeasible 28 times.

### R6c — whether sharing a location settles a pair. **Adopted, contested.**

This is the one genuinely unresolved interpretation in the project, and the brief
contradicts itself on it. Rule 6 reads:

> "Same `(location_id, week, co_share_group)` = one possession (one access-night
> slot) — no buffers between them, exempt from each other's closures. Different
> `co_share_group` values at the same location/week are **separate possessions on
> separate nights** within that week's allocation, **and buffers apply normally
> between them**."

The two emphasised clauses cannot both bite. A buffer is a spatial exclusion *on a
given night*; if two possessions are on separate nights by construction, there is
nothing for a buffer between them to forbid. The sentence asserts both.

| | Reading | Consequence |
| --- | --- | --- |
| **Adopted** | Sharing a location settles the pair: identical `co_share_group` means one possession (exempt by the first clause); a different one means provably different nights, so no spatial conflict arises. | Permissive |
| **Literal** | The second clause is taken at face value: a buffer reaching a co-located activity's worksite is a breach even when the two share a location. | Restrictive |

**Evidence against the literal reading.**

1. It declares the organisers' own `03_submission_sample/` infeasible **24 times**,
   contradicting the brief's explicit statement that the sample has 0 hard
   violations. Reproduce with `python3 tools/derive/exposure.py`.
2. It is schedulable, but its *proven optimum* on the public instance is worse
   than the score the sample actually achieves:

   | | Scenario A | B | C |
   | --- | --- | --- | --- |
   | adopted reading, our optimum | **32.2** | **30.0** | **26.1** |
   | literal reading, our optimum | 93.8 | 70.0 | 87.7 |
   | shipped sample (Scenario A) | 48.3 | — | — |

   The sample scores 48.3, better than the best plan the literal reading permits
   (93.8). A submission cannot outperform the optimum of the rules it obeys, so
   the sample was not produced under the literal reading.

That is strong, but it is not proof: it rests on the sample being current and on
the organisers' 0-violation claim. Hence **contested**, not settled.

**Our exposure if we are wrong.** Measured by `tools/derive/exposure.py`, which
counts activity pairs where a zone reaches beyond its owner's own worksite into
another activity's worksite in the same week:

| Submission | breaches, adopted reading | breaches, literal reading |
| --- | --- | --- |
| shipped sample | 0 | 24 |
| ours, Scenario A | 0 | ~23 |
| ours, Scenario B | 0 | ~42 |
| ours, Scenario C | 0 | ~31 |
| ours with `--strict-buffers`, A / B / C | 0 | **0 / 0 / 0** |

Our figures are approximate because an instance usually has several plans at the
same optimal objective, and they do not all expose identically; the `0` and the
`0 / 0 / 0` are exact. **Re-run `python3 tools/derive/exposure.py` against the
outputs you actually hold rather than trusting these numbers.**

**Two corrections, both withdrawing earlier claims.**

1. An earlier draft asserted that "our solver is never less strict than the
   evidence allows." That is **wrong and withdrawn**. Under the literal reading our
   default outputs do contain breaches.
2. A later draft reported the sample at 50 breaches and our outputs at 6 / 27 / 0,
   and concluded our exposure was "strictly smaller than the reference sample's".
   Those counts came from a measurement that wrongly treated any shared location as
   a closure clash, when two activities at one location on different nights are
   governed by capacity, not by the closure rule. **The corrected counts are above,
   and the conclusion is withdrawn: our exposure is comparable to the sample's for
   Scenario A and larger for B and C.** The tool is now in the repository so the
   figure can be re-derived rather than trusted.

What survives both corrections is the argument against the literal reading itself
— which rests on the sample, not on us — and the fact that `--strict-buffers`
produces plans with zero breaches under either reading.

**The hedge.** `trackaccess solve --strict-buffers` enforces the literal reading,
producing plans valid under **both**. It costs roughly 2.3×–3.4× on the objective.
`out/public-strict/` holds those plans alongside the primary ones so the trade is
visible rather than theoretical. Which to submit is a judgement call recorded in
`DEBUG_HANDOFF.md`, not a decision this document makes.

**What would settle it.** Running the official validator on the shipped sample. If
it reports 0 violations, the literal reading is dead. If it reports ~50, the
adopted reading is dead and `--strict-buffers` becomes the default. Nothing short
of that resolves it.

## R6d. "Buffers never overlap" at face value — **refuted**

> "Buffers never overlap — a `Live`/`Non-Live(Consist)` work whose buffer reaches,
> say, `S02` pushes the next `Live`/`Non-Live(Consist)` work on that bound to
> start no earlier than `S03`."

Read at face value, the first clause forbids two buffered possessions whose
exclusion zones touch at all, even where neither zone reaches the other's
worksite. Two independent pieces of evidence refute that reading:

1. The shipped sample breaches it **7 times**.
2. **No schedule can satisfy it.** Enforcing it in the solver makes the public
   instance infeasible: Scenarios A and B are *proven* infeasible and C does not
   finish. A rule under which the published problem has no answer at all cannot
   be the rule the problem is scored by.

The second point is the stronger one, because it does not depend on the sample
being current. Reproduce it with:

```bash
./build/trackaccess solve --data data/upstream/PS1/01_data --out /tmp/x \
  --scenario A --seconds 30 --no-zone-overlap        # -> proven infeasible
```

What the clause is taken to mean instead is the *second* half of the sentence,
which is concrete: a buffer pushes the next buffered work far enough away that it
does not stand inside the buffered zone. That is R6b, and it is enforced.

**Three nested readings are implemented**, so the cost of each is measurable
rather than arguable:

| Reading | Flag | Never-same-week pairs | Public instance |
| --- | --- | --- | --- |
| adopted | *(default)* | 58 | A 32.2 · B 30.0 · C 26.1 |
| literal exemption (R6c) | `--strict-buffers` | 116 | A 93.8 · B 70.0 · C 87.7 |
| zones may not touch (R6d) | `--no-zone-overlap` | 125 | **infeasible** |

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
