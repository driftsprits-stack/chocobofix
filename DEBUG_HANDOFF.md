# Debug handoff

Written for the next session — human or assistant — to pick this up without
re-deriving what is already settled. Read `docs/DERIVED_RULES.md` first; it is
the part that took the longest and is the part most likely to be wrong.

## Where things stand

Working and verified on arm64 macOS 15.6.1:

- Import of the eight CSVs with file/row/field error reporting.
- CP-SAT scheduling for Scenarios A, B and C. All three reach **proven optimality**
  on the public instance in well under a second.
- An independent checker that accepts the organisers' own sample submission as
  feasible, and a second implementation in Python that agrees with it.
- CLI (`solve`, `validate`, `diagnose`) and an HTTP service with a job queue and
  worker processes.
- Browser interface: import → generate → schedule → network → check → repair →
  export, in four languages.
- "Why not earlier?" (`explain`), disruption repair (`repair`), before/after
  comparison (`compare`) and infeasibility attribution (`diagnose`), each exposed
  on the CLI and, for the first two, in the interface.
- 77 unit/mutation/metamorphic checks, 63 integration/security checks, ASan+UBSan
  clean.

Not built at all: what-if beyond reduced access, user accounts, roles, approvals,
plan versioning, TLS, CI. `docs/REQUIREMENTS_MATRIX.md` is the authoritative list
and is written to be believed rather than to look complete.

Not done as submission steps: **not deployed** (no URL), **not recorded** (no
video), **not pushed** (no GitLab remote).

## Reproduce the current results

```bash
./scripts/fetch_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/check_sample                       # must print SAMPLE ACCEPTED
./build/test_core                          # must print 77 passed, 0 failed
./tests/integration.sh build               # must print 63 passed, 0 failed
./build/trackaccess solve --data data/upstream/PS1/01_data --out out/public \
  --scenario all --seconds 120 --workers 8
```

Expected objectives: A 32.2, B 30.0, C 26.1, all feasible and proven optimal.
If any of those three numbers moves, something in the rule model changed —
find out what before trusting the new number.

## The one thing to be suspicious of

**`docs/DERIVED_RULES.md` R6, the closure/buffer conflict rule.** The official
reference validator is not published, so the rule set is reconstructed from the
brief and corroborated only by the fact that it declares the shipped sample
feasible. R6 has a genuinely unverifiable corner: when two activities share *no*
location, nothing in the output schema establishes whether they run on the same
night, because `access_night` is a per-contract accounting index rather than a
network-wide night id. We take the conservative branch.

Consequences if we are wrong:
- If the real validator is **more permissive**, we leave score on the table and
  may declare infeasible something that is not.
- If it is **stricter**, no rule consistent with a feasible sample could be, so
  this direction is safe.

`trackaccess diagnose` shows that lifting this rule does **not** restore
feasibility on the dense synthetic instances, so it is not currently the binding
constraint. That is evidence, not proof, that it is not costing us much.

## Known rough edges

0. **The churn preference must stay out of the competition objective.** `repair`
   adds a small weight for keeping baseline assignments. It is a tie-break only,
   and `SolveResult::objective_tenths` is deliberately recomputed from the
   extracted plan rather than taken from CP-SAT's objective value, which in
   repair mode carries the churn term. If those two are ever conflated, scored
   runs start optimising the wrong thing and nothing will obviously break.


1. **Scenario B may be infeasible on a harder instance.** B forbids any overrun.
   If a hidden instance cannot meet every planned date even with unlimited ECLO
   and extra nights, B correctly returns `infeasible` and exports nothing.
   `--fallback` covers that case: it prices the scenario policy instead of
   enforcing it, keeps every physical safety rule hard, and marks the output
   `NOT_SUBMISSION_READY`. A test asserts the only resulting breaches are
   `planned_date` — never a safety rule. **Decide deliberately whether to pass
   `--fallback` for the hidden-instance run.** It is off by default because an
   unsubmittable file should not look submittable; it is available because
   nothing at all scores nothing at all.

2. **The worker-crash test is weakly exercised.** The public instance solves in
   under a second, so `pkill` usually lands after completion. The service's
   survival is genuinely verified; the mid-solve path is not. To exercise it,
   use a synthetic instance with a long budget.

3. **`ComputeOccupancy` packs slots greedily after the search.** It is proven
   minimal for every composition up to 3 PM / 4 PC / 12 C (`test_core`), and the
   solver's capacity inequalities guarantee a legal packing exists. If a future
   change makes the solver and the packer disagree, the symptom is a `capacity`
   violation appearing only in the post-export re-check — which is exactly where
   it should appear, and why the re-check exists.

4. **The ECLO continuity window (Scenario C) is modelled per line over the whole
   plan**, with one window per line shared by every activity. The brief says the
   window is "chosen independently per line", which this satisfies, but if it
   were meant per contract or per activity the model is too strict. Untested
   against any official artefact — there is no Scenario C sample.

5. **Four-language strings are unreviewed.** `web/i18n.js` says so in its header.
   Get a fluent speaker to read ms, zh and ta before showing this to judges as a
   localisation feature.

## Next highest-value work, in order

1. **Deploy it.** Deliverable §4.2 is a hosted URL and it is the one scored item
   that is entirely undone. `docs/DEPLOYMENT.md` is a complete procedure; it needs
   a host and a domain, nothing more. Perhaps two hours.
2. **Record the video.** `docs/DEMO_SCRIPT.md` is timed and rehearsable.
3. **Widen what-if.** Reduced access already works (it is the repair mechanism).
   Increased workload and reduced workfront availability are not exposed and
   would each be a small addition to `SolveOptions`.
4. **Persist repairs as plan versions.** `repair` currently writes to a directory
   you name and does not record lineage. Storing baseline → repair as a version
   chain is the first step toward approvals.
5. Multi-user, if the brief's operational requirements matter more than
   competition score. This is the largest remaining gap and needs a real
   transactional store first.

## Layout reminders

- `src/core/instance.cpp` — `BuildTopology` holds span expansion, closure and
  buffer zones, and the never-same-week pair set. This is where R6 lives.
- `src/solver/solver.cpp` — one function builds the whole model; constraint groups
  are individually liftable through `SolveOptions::relax`, which `diagnose` uses.
- `src/validator/validator.cpp` — deliberately does not share constraint code with
  the solver. Keep it that way; it is the only reason a solver bug is catchable.
- `tools/derive/crosscheck.py` — the second implementation. If you change a rule
  in C++, change it here too and re-run both, or the cross-check silently stops
  being evidence.

## Secrets

None in the repository. The service generates a bearer token at startup unless
one is passed; it is printed to stdout and never written into the store. There is
no configuration file holding credentials, because there are no credentials.
