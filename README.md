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
| Tests | 120 core + 94 store + 132 integration/security, ASan+UBSan clean |
| Multi-user | accounts, roles, sessions, plan versions, validation-bound approvals, audit — all through the interface |
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

## Clean-machine setup

Verified on a clean checkout on arm64 macOS 15.6.1. Each step is a command you
can paste; nothing else is required.

### 1. Prerequisites

| Need | macOS | Debian / Ubuntu |
| --- | --- | --- |
| C++20 compiler | Xcode Command Line Tools (`xcode-select --install`) | `build-essential` (GCC 12+) |
| CMake ≥ 3.20 | `brew install cmake` | `cmake` |
| SQLite headers | in the macOS SDK, nothing to install | `libsqlite3-dev` |
| Password hashing | CommonCrypto, in the SDK | `libssl-dev` (OpenSSL) |
| `curl`, `tar`, Python 3.9+ | preinstalled | `curl ca-certificates python3` |

```bash
# Debian / Ubuntu, one line
sudo apt-get install -y build-essential cmake curl ca-certificates                         libsqlite3-dev libssl-dev python3
```

Python is used only by the cross-check and test tools, never by the application
at runtime. **No other system packages are needed.** OR-Tools is fetched as a
pinned binary into `third_party/` — not installed system-wide, no sudo, no
Homebrew formula.

### 2. Fetch dependencies and build

```bash
./scripts/fetch_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`fetch_deps.sh` downloads OR-Tools 9.14.6206 for the host triple and
cpp-httplib 0.18.3, printing and verifying SHA-256 for both. Roughly 46 MB.

### 3. Prove the build before trusting it

```bash
./build/test_core        # 90 checks: rules, counterexamples, mutations
./build/test_store       # 91 checks: accounts, roles, approvals, concurrency
./build/check_sample     # our checker must accept the organisers' own sample
```

If any of those fail, stop — something in the rule model or the store changed.

## Exact local launch command

Single planner, offline, on this machine:

```bash
./build/trackaccess-service   --host 127.0.0.1 --port 8080   --root ./var   --web ./web   --worker ./build/trackaccess   --public-instance ./data/upstream/PS1/01_data
```

Then open **http://127.0.0.1:8080/**. With no accounts yet, the sign-in screen
offers to create the first administrator. To skip that prompt:

```bash
./build/trackaccess-service   --host 127.0.0.1 --port 8080 --root ./var --web ./web   --worker ./build/trackaccess   --public-instance ./data/upstream/PS1/01_data   --bootstrap-admin "admin:choose-a-long-password"
```

`--bootstrap-admin` is ignored once any account exists. Other options:
`--max-solves` (default 2), `--max-seconds` (120), `--session-idle` (1800),
`--session-max` (28800).

The administrator then creates `planner`, `approver` and `viewer` accounts from
the **Accounts** tab. Those four roles are what the workflow below assumes.

For a shared or hosted deployment, put TLS in front and follow
`docs/DEPLOYMENT.md`. The service binds loopback by default and warns if told
to bind anywhere else.

## Using it

The workspace has four places: **Overview, Schedule, Activities, History**, plus
**Settings**. Creating a plan is a separate four-step flow with named stages:

**Upload → Check → Generate → Review**

1. **Upload** — drop the eight CSVs, or press *Try sample project*. Files you have
   already chosen are kept if a later selection is partial.
2. **Check** — a plain summary of jobs, contracts and the planning window before
   any dense table. If the data is wrong, every problem is listed with file, row,
   field and what to correct; you can replace one file without starting over.
3. **Generate** — the three scenarios carry plain labels ("Keep existing access
   limits", "Meet planned completion dates", "Allow limited extra access") with a
   sentence each, and a panel listing what never changes whichever you pick.
   Solver settings are behind *Advanced*. There is no progress percentage,
   because the solver cannot say how far through it is.
4. **Review** — what happened, in a sentence, before the numbers: whether all
   work is scheduled, which contracts finish late, and what extra access it cost.

Then **Schedule** is the working area — a timeline where each bar is one night,
a per-week network view, and a detail panel that answers *"why not another
week?"* for any job. **Adjust schedule** applies a disruption and shows the
consequences before anything is accepted. **History** holds every version, who
made it, who published it, and the activity log.

A version is only *Published* when an approver signs it off, and the approval is
bound to the exact plan and check result they were shown.

Interface languages: English, Bahasa Melayu, 简体中文, தமிழ். Switching language
keeps you where you are. Text size adjusts from the header. Every control is
keyboard reachable; no interaction needs colour, hover or dragging alone.

**"Checked" means the plan passed our own rule checker.** It is not official
certification and not permission to dispatch work. The interface says so on the
review screen and in Help.

## Running the solver from the command line

```bash
./build/trackaccess solve   --data data/upstream/PS1/01_data   --out  out/public   --scenario all --seconds 120 --workers 8
```

Writes `out/public/{A,B,C}/` each containing `SCHEDULE_ACCESS.csv`,
`SCHEDULE_OCCUPANCY.csv`, `RESULTS.csv` and `VALIDATION.json`. The exported
files are re-read and re-checked before the run reports success.

`--scenario` takes `A`, `B`, `C` or `all`. `--seconds` is the per-scenario
budget; `--seed` makes a run reproducible; `--strict-buffers` uses the stricter
reading of rule 6 (see `docs/DERIVED_RULES.md` R6c). Ctrl-C stops the search and
keeps the best complete plan already found.

Exit codes: `0` success · `1` usage or input error · `2` no plan produced ·
`3` the plan failed the independent check.

### When a scenario's policy cannot be met

Scenario B forbids any overrun, and Scenario A forbids any extra access-night. On
a sufficiently tight instance those policies are unsatisfiable, and the tool then
reports `infeasible` and **exports nothing** — an unsubmittable plan should not
look submittable.

`--fallback` changes that: the scenario's own policy is priced instead of
forbidden, while **every physical safety rule stays hard**. The result is written
with a `NOT_SUBMISSION_READY.txt` beside it, the run says so loudly, and the
store **refuses to approve it** no matter who asks. Use it to see how far out of
policy an instance is; never submit it as a conforming answer.

## Explain, repair, compare## Explain, repair, compare

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
./build/test_core                    # 120 rule, counterexample and mutation checks
./build/test_store                   # 94 account, role, approval and concurrency checks
./build/check_sample                 # our checker must accept the upstream sample
./tests/integration.sh build         # 132 end-to-end, API and security checks
python3 tests/test_multiuser.py      # the shared-project suite on its own
ctest --test-dir build               # runs the three C++ suites under CTest
```

## Platform and feature support

**Tested** means built and executed here, with the result recorded in
`docs/TEST_REPORT.md`. Nothing else is claimed.

| Platform | Build | Tests | Notes |
| --- | --- | --- | --- |
| **arm64 macOS 15.6.1** | **tested** | **tested** | Apple clang 17, CMake 4.4.3, OR-Tools 9.14.6206 arm64 |
| x86_64 macOS | not built | not run | `fetch_deps.sh` selects the right asset; unverified |
| x86_64 / arm64 Linux | **not built** | **not run** | CMake is portable and an OR-Tools asset is selected, but nothing was compiled or executed |
| Windows | not attempted | — | no configuration written |
| Container image | **never built** | — | `deploy/Dockerfile` is unverified; see `docs/DEPLOYMENT.md` |

| Feature | Status |
| --- | --- |
| Scenarios A / B / C, proven optimal on the public instance | tested |
| Independent checker, cross-checked by a second implementation | tested |
| Explain, repair, compare, diagnose, fallback, strict-buffers | tested |
| Accounts, roles, sessions, project isolation | tested |
| Plan versions, validation-bound approvals, revocation, invalidation | tested |
| Optimistic concurrency, audit trail | tested |
| Four-language interface | 302 strings x 4 languages, none missing; **ms/zh/ta unreviewed by a fluent speaker** |
| TLS-terminated hosted shape | tested locally with a throwaway certificate; **not deployed** |
| Backup / restore | procedure written; **restore never rehearsed** |
| CI | **none configured** |

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
