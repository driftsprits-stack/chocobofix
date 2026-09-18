# Debug handoff

For the next session — human or assistant — to pick this up without re-deriving
what is already settled. Read `docs/DERIVED_RULES.md` first: it is the part that
took longest and the part most likely to be wrong.

Commit at the time of writing: run `git rev-parse HEAD`.

---

## What is built and verified

Everything below was built and executed on **arm64 macOS 15.6.1** (Apple clang
17, CMake 4.4.3, OR-Tools 9.14.6206). That is the only platform tested.

**Scheduling core**
- Import of the eight CSVs with file/row/field error reporting.
- CP-SAT scheduling for Scenarios A, B and C. All three reach **proven
  optimality** on the public instance in under half a second.
- An independent checker that accepts the organisers' own sample submission as
  feasible, plus a second implementation in Python that agrees with it. (Those
  two agreeing shows implementation fidelity, not that the interpretation is
  right — `docs/DERIVED_RULES.md` opens with why.)
- `explain` ("why not earlier?"), `repair` (disruption), `compare`, `diagnose`
  (infeasibility attribution), `--fallback`, `--strict-buffers`.

**Shared-project layer**
- SQLite store owned solely by the service; WAL, `synchronous=FULL`.
- Accounts and four roles, passwords via the platform's PBKDF2.
- Sessions with idle and absolute expiry, revoked on disable and role change.
- Immutable plan versions; approvals bound to content and validation hashes.
- Optimistic concurrency on projects; audit trail with correlation ids.
- All of it usable through the interface, not just the API.

**Test counts**: 90 core, 91 store, 121 integration (including a 59-check HTTP
suite). ASan + UBSan clean on the core and checker.

## What is NOT built, and not claimed

- **No Linux or Windows build exists.** Not compiled, not run.
- **`deploy/Dockerfile` has never been built.** No container runtime here.
- **Not deployed. No URL.** The TLS *shape* was tested locally with a throwaway
  certificate; `docs/DEPLOYMENT.md` says exactly what access is needed.
- **No video recorded**, **no push to GitLab** (no remote, no credentials).
- No CI, no coverage measurement, no multi-organisation tenancy, no offline
  client mode, no tamper-evident audit.
- **ms / zh / ta interface strings are unreviewed by a fluent speaker.**
- **No backup restore has been rehearsed**, so no RTO/RPO is measured.

`docs/REQUIREMENTS_MATRIX.md` is the authoritative list and is written to be
believed rather than to look complete.

## Reproduce the current results

```bash
./scripts/fetch_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/check_sample                       # must print SAMPLE ACCEPTED
./build/test_core                          # must print 90 passed, 0 failed
./build/test_store                         # must print 91 passed, 0 failed
./tests/integration.sh build               # must print 121 passed, 0 failed
./build/trackaccess solve --data data/upstream/PS1/01_data --out out/public \
  --scenario all --seconds 120 --workers 8
```

Expected objectives: **A 32.2, B 30.0, C 26.1**, all feasible and proven
optimal. If any of those three numbers moves, something in the rule model
changed — find out what before trusting the new number.

## The one thing to be suspicious of

**`docs/DERIVED_RULES.md` R6c — whether sharing a location settles a pair.**

Rule 6 in the brief contradicts itself: different `co_share_group` values at one
location/week are "separate possessions on separate nights" *and* "buffers apply
normally between them". Those cannot both bite.

We take the permissive reading. The evidence is that the literal reading declares
the organisers' own stated-0-violation sample infeasible 24 times, and that the
sample's actual score (48.3) is better than the best plan the literal reading
permits (93.8) — so the sample cannot have been produced under it. That is
strong, but it rests on the sample being current, so R6c is labelled **adopted,
contested**, not settled.

Our default outputs breach the literal reading roughly 23 / 42 / 31 times
(A / B / C) — approximate, because several plans share the same optimal
objective and they do not all expose identically. The sample breaches it 24
times. Re-run `tools/derive/exposure.py` rather than trusting those figures. `--strict-buffers` produces plans clean under
**both** readings at 2.3×–3.4× the objective, and `out/public-strict/` holds
them. Measure any submission with:

```bash
python3 tools/derive/exposure.py data/upstream/PS1/01_data out/public/A
```

**What would settle it:** running the official validator on the shipped sample.
Nothing short of that does. The validator is not published in the problem
repository — verified over the full recursive tree.

Two counterexample tests in `tests/test_core.cpp` (group "rule 6
counterexamples") pin each reading's verdict on a named pair in
`tests/data/micro`. If those assertions ever flip, the interpretation has changed
silently.

## Known rough edges

1. **The churn preference must stay out of the competition objective.** `repair`
   adds a small weight for keeping baseline assignments. It is a tie-break only,
   and `SolveResult::objective_tenths` is deliberately recomputed from the
   extracted plan rather than taken from CP-SAT's objective value, which in
   repair mode carries the churn term. Conflating them would silently optimise
   the wrong thing in scored runs.

2. **Decide deliberately whether to pass `--fallback` for the hidden-instance
   run.** Off by default because an unsubmittable file should not look
   submittable; available because nothing at all scores nothing at all. A test
   asserts a fallback plan's only breaches are `planned_date` — never a safety
   rule — and the store refuses to approve one.

3. **The worker-crash test is timing-dependent.** The public instance solves in
   under a second, so `pkill` sometimes lands after completion. The service's
   survival is genuinely verified; the mid-solve path is not deterministic. Use a
   synthetic instance with a long budget to exercise it reliably.

4. **`ComputeOccupancy` packs slots greedily after the search.** Proven minimal
   for every composition up to 3 PM / 4 PC / 12 C. If a future change makes the
   solver and the packer disagree, the symptom is a `capacity` violation
   appearing only in the post-export re-check — which is where it should appear,
   and why the re-check exists.

5. **The Scenario C ECLO window is modelled per line over the whole plan**, one
   window per line shared by every activity. The brief says "chosen independently
   per line", which this satisfies, but if it were meant per contract or per
   activity the model is too strict. No Scenario C sample exists to check against.

## Next highest-value work, in order

1. **Deploy it.** §4.2 is a hosted URL and it is the one scored item entirely
   undone. `docs/DEPLOYMENT.md` names three options and exactly what access each
   needs. A custom domain is not required.
2. **Build the container image once**, on any Linux host, to turn
   `deploy/Dockerfile` from written into verified.
3. **Record the video.** `docs/DEMO_SCRIPT.md` is timed and rehearsable.
4. **Get the four-language strings reviewed** before presenting localisation as
   a feature.
5. Widen what-if: reduced access works (it is the repair mechanism); increased
   workload and reduced workfront availability are not exposed and would each be
   a small addition to `SolveOptions`.

## Layout reminders

- `src/core/instance.cpp` — `BuildTopology` holds span expansion, closure and
  buffer zones, and **both** never-same-week pair sets. This is where R6 lives.
- `src/solver/solver.cpp` — one function builds the whole model; constraint
  groups are individually liftable via `SolveOptions::relax`, which `diagnose`
  uses.
- `src/validator/validator.cpp` — deliberately shares no constraint code with the
  solver. Keep it that way; it is the only reason a solver bug is catchable.
- `src/service/store.cpp` — `RoleHas` is the entire permission matrix;
  `ApprovePlan` is where the refusals live. Both are heavily tested.
- `tools/derive/crosscheck.py` — the second rule implementation. If you change a
  rule in C++, change it here too and re-run both, or the cross-check silently
  stops being evidence.
- `tools/derive/exposure.py` — measures R6c exposure. Re-run it rather than
  trusting the numbers quoted in the docs.

## Secrets

None in the repository. Passwords are stored as PBKDF2 hashes; sessions are
stored as token hashes. `--bootstrap-admin` takes a password on the command line,
which is visible in the process list — use it only for a first run on a machine
you control, and change the password through the interface afterwards. The only
token-shaped string in the tree is a test fixture in `tests/integration.sh`.
