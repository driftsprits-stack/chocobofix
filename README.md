# TrackAccess — PS1 Railway Track Access Optimisation

A decision-support tool for railway possession planning on the dual-line
Alpha/Beta network: it imports the eight instance CSVs, generates a possession
schedule for Scenarios A, B and C with CP-SAT, checks the result against the
operating rules with an independently written checker, and exports the three
competition files per scenario.

**Central principle: a planner should be able to see the consequences of a
decision before committing to it.** Every figure in the interface is computed by
re-reading the files that were actually written, not from a parallel copy held
in memory.

---

## Status at a glance

| | |
| --- | --- |
| Public-instance result | Scenarios A, B, C all **proven optimal**, 0 hard violations under our checker |
| Solve time (all three) | **0.48 s** wall, 54 activities / 14 contracts / 76 locations / 30 weeks |
| Corroboration | our checker accepts the upstream `03_submission_sample/` as feasible (0 violations) |
| Tests | 77 unit + 57 integration/security, ASan+UBSan clean |
| Hosted deployment | **not deployed** — see [Deployment](#deployment). No URL exists yet. |
| Video | **not recorded** — script in `docs/DEMO_SCRIPT.md` |

Objective scores on the public instance (lower is better; every term is a penalty):

| Scenario | Overrun days | Extra access-nights | ECLO nights | Objective | Optimality |
| --- | --- | --- | --- | --- | --- |
| A | 28 | 0 | 0 | **32.2** | proven |
| B | 0 | 0 | 6 | **30.0** | proven |
| C | 14 | 0 | 2 | **26.1** | proven |

For reference, the upstream sample submission (Scenario A) scores **48.3** by the
same measure, under the same checker. Optimality is proven *with respect to our
model of the rules* — see `docs/DERIVED_RULES.md` for where that model is
corroborated and where it is a conservative reading.

---

## Prerequisites

- A C++20 compiler. Developed with Apple clang 17; GCC 12+ / clang 15+ should work.
- CMake ≥ 3.20.
- `curl`, `tar`, and Python 3.9+ (Python is used only by the cross-check and
  derivation tools, never by the application at runtime).

No other system packages are required. OR-Tools is fetched as a pinned binary
distribution into `third_party/` — it is **not** installed system-wide and needs
no Homebrew, apt, or sudo.

## Build

```bash
./scripts/fetch_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Produces `build/trackaccess` (CLI) and `build/trackaccess-service` (HTTP service).

## Run the solver

```bash
./build/trackaccess solve \
  --data data/upstream/PS1/01_data \
  --out  out/public \
  --scenario all --seconds 120 --workers 8
```

Writes `out/public/{A,B,C}/` each containing `SCHEDULE_ACCESS.csv`,
`SCHEDULE_OCCUPANCY.csv`, `RESULTS.csv` and `VALIDATION.json`. The exported files
are re-read and re-checked before the run reports success.

`--scenario` takes `A`, `B`, `C` or `all`. `--seconds` is the per-scenario search
budget; `--seed` makes a run reproducible. Ctrl-C stops the search and keeps the
best complete plan already found.

Exit codes: `0` success · `1` usage or input error · `2` no plan produced ·
`3` the plan failed the independent check.

## Explain, repair, compare

**Why not earlier?** — test whether an activity could take an access in a given
week, and if not, which rule stands in the way:

```bash
./build/trackaccess explain --data data/upstream/PS1/01_data \
  --activity A004 --week 16 --scenario A
```

It answers one of three things, and never confuses them: *yes* (with the cascade
it causes and its cost), *proven impossible* (naming the binding rule), or *not
established within the budget* — which is not evidence of impossibility.

**Disruption repair** — urgent maintenance takes nights away from a location;
re-plan around it while keeping unaffected commitments:

```bash
./build/trackaccess repair --data data/upstream/PS1/01_data --out out/repaired \
  --supply "SEC:BET:H01_H02:EB@15=0" --supply "SEC:BET:H01_H02:EB@16=0" \
  --scenario A
```

Reports the objective before and after, how many activity-weeks moved, and which
activities changed. The minimal-change preference is a tie-break only: it is
**never** part of the competition objective, and the score reported afterwards is
recomputed from the written plan without it.

**Before/after** — diff two exported submissions:

```bash
./build/trackaccess compare --data data/upstream/PS1/01_data \
  --before out/public/A --after out/repaired
```

**Diagnose an infeasible instance** — lift one rule group at a time to find which
one binds:

```bash
./build/trackaccess diagnose --data <instance> --scenario A
```

## Check a submission

```bash
./build/trackaccess validate \
  --data data/upstream/PS1/01_data \
  --submission out/public/A
```

Prints the JSON conformance report and exits `3` if any hard rule is breached.
An independently written Python implementation of the same rules is available for
cross-checking — agreement between the two is evidence, since they share no code:

```bash
python3 tools/derive/crosscheck.py data/upstream/PS1/01_data out/public/A
```

## Run the web application

Offline / single planner (loopback only, token required):

```bash
./build/trackaccess-service \
  --host 127.0.0.1 --port 8080 \
  --root ./var --web ./web --worker ./build/trackaccess \
  --public-instance ./data/upstream/PS1/01_data
```

It prints a bearer token; open `http://127.0.0.1:8080/?token=<token>`.

Judging / shared deployment (no token, put TLS in front — see Deployment):

```bash
./build/trackaccess-service \
  --host 0.0.0.0 --port 8080 --auth none \
  --root ./var --web ./web --worker ./build/trackaccess \
  --public-instance ./data/upstream/PS1/01_data
```

Workflow in the interface: **Import → Generate → Schedule → Network → Check →
Repair → Export**. Drop the eight CSVs (or press *Load public instance*), pick a scenario,
generate, then inspect the timeline, the network occupancy per week, the
conformance report, and download the competition files. Selecting an activity
shows the locations it books and its buffer, and offers "why not another week?"
for any week you name. The Repair tab applies a disruption and shows what it
costs before you accept it.

Interface languages: English, Bahasa Melayu, 简体中文, தமிழ். Text size is
adjustable from the header. Every control is keyboard reachable; there is no
drag-only interaction.

## Tests

```bash
./build/test_core              # 77 unit, mutation and metamorphic checks
./build/check_sample           # our checker must accept the upstream sample
./tests/integration.sh build   # 57 end-to-end, API and security checks
ctest --test-dir build         # runs the first two under CTest
```

Sanitizers:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j && ASAN_OPTIONS=detect_leaks=0 ./build-asan/test_core
```

Measured results are in `docs/TEST_REPORT.md`.

## Deployment

**Nothing is deployed. No hosted URL exists.** The service is ready to run behind
a reverse proxy; the exact procedure, including TLS termination and the
certificate rotation note, is in `docs/DEPLOYMENT.md`. That step needs hosting
credentials this project does not have, so it is left for the operator.

## Repository layout

```
src/core/        domain model, strict CSV I/O, topology expansion, scoring, export
src/solver/      CP-SAT model for Scenarios A / B / C
src/validator/   independent conformance checker (shares no constraint code)
src/cli/         trackaccess command line
src/service/     HTTP application service, job queue, worker supervision
web/             browser interface (no framework, no build step, 4 languages)
tests/           unit, mutation, metamorphic, integration and security tests
tools/derive/    re-runnable rule-derivation and cross-check scripts
docs/            derived rules, architecture, requirements matrix, test report
data/upstream/   vendored PS1 instance at a recorded commit, with hashes
out/public/      generated results for the public instance
```

## Licences

This project is provided for the NebulaX hackathon submission.
Third-party components, fetched by `scripts/fetch_deps.sh` and not vendored in
this repository:

- **OR-Tools 9.14.6206** — Apache License 2.0. Bundles abseil-cpp, Protocol
  Buffers, RE2, SCIP, SoPlex, HiGHS, CBC/CLP/CGL/OSI/CoinUtils, Boost and zlib;
  their notices ship inside the distribution under `third_party/or-tools_*/share/doc`.
- **cpp-httplib 0.18.3** — MIT License.

SHA-256 of both is printed and verified by `scripts/fetch_deps.sh`.
The PS1 instance data under `data/upstream/` belongs to the problem authors and
is vendored unmodified at commit `966c976`.
