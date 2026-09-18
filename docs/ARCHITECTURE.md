# Architecture

## Shape

One modular application, not a fleet of services. Three binaries share one core
library; the browser interface is static files served by the application service.

```
                          ┌─────────────────────────────────────┐
  browser ───── HTTPS ───▶│ trackaccess-service   (C++)         │
  (web/, no build step)   │  · owns ALL writes to the store     │
                          │  · auth, upload limits, job queue   │
                          │  · supervises worker processes      │
                          └───────────────┬─────────────────────┘
                                          │ fork + execv, rlimits
                                          ▼
                          ┌─────────────────────────────────────┐
                          │ trackaccess  (CLI = solve worker)   │
                          │  import → solve → export → re-check │
                          └───────────────┬─────────────────────┘
                                          │
   ┌──────────────────────────────────────┴──────────────────────────────┐
   │ ta_core                                                             │
   │  core/     model · strict CSV I/O · topology expansion · scoring    │
   │  solver/   CP-SAT model (OR-Tools 9.14, pinned)                     │
   │  validator/ independent conformance checker                         │
   └─────────────────────────────────────────────────────────────────────┘
```

## Trust boundaries

| Boundary | What crosses it | Control |
| --- | --- | --- |
| browser → service | instance CSVs, job requests | bearer token (constant-time compare), 32 MiB payload cap, 500k row cap, ids restricted to `[a-z0-9]{1,40}` |
| service → worker | a directory path | separate process; `RLIMIT_AS`, `RLIMIT_CPU`, `RLIMIT_FSIZE`; writes confined to its own job directory |
| worker → store | output CSVs | written to a temp file, `fsync`ed, then `rename`d over the target |
| service → browser | JSON, CSV | CSP without `unsafe-eval`, `nosniff`, `DENY` framing, no CORS headers emitted at all |

The service is the only writer. Clients never open store files directly, so there
is no shared-file-on-a-network-share arrangement to corrupt.

By default the service binds `127.0.0.1` and requires a token. Binding elsewhere
is an explicit `--host` choice and prints a warning that TLS belongs in front.

## Why a separate worker process

A native solver fault, an out-of-memory condition, or a runaway search must not
take down the interface. `fork` + `execv` of the CLI gives:

- crash containment — a worker dying is one failed job (`tests/integration.sh`
  kills a worker mid-run and asserts the service stays healthy);
- resource bounds enforced by the kernel rather than by hope;
- one code path — the worker is the same binary and the same import→solve→export→
  re-check sequence that the CLI runs, so the hosted result cannot drift from the
  command-line result.

## ADR-1 · Pinned OR-Tools binary instead of a package manager

`brew install or-tools` pulls a large source-build dependency chain (it began
fetching GCC on the development machine). The official pinned binary distribution
for the host triple is fetched into `third_party/`, needs no sudo, and fixes the
solver version at `9.14.6206` for reproducibility. `ORTOOLS_ROOT` overrides it for
platforms with no published binary.

## ADR-2 · The checker does not reuse the solver's constraints

`src/validator/` re-derives every rule from the instance and the emitted files. It
shares schema parsing and topology expansion with the solver — both must read the
same network — but no constraint construction. A checker built from the solver's
own model could only ever confirm the solver's own mistakes.

A second, independent implementation of the same rules exists in Python
(`tools/derive/crosscheck.py`), written from the brief rather than from the C++.
Agreement between two implementations that share no code is evidence; a single
implementation agreeing with itself is not.

## ADR-3 · Filesystem store, not SQLite

The store holds immutable instance snapshots and per-job output directories. Both
are naturally files, are written once and read many times, and are exactly what
the competition asks to be exported. Atomic `write → fsync → rename` gives the
durability the plan needs.

**Consequence, stated plainly:** this is not a multi-user database. Accounts,
roles, approvals, plan versioning and optimistic concurrency are **not
implemented** — see `docs/REQUIREMENTS_MATRIX.md`. A future multi-planner
deployment needs a real transactional store; the service already funnels every
write through one process, which is the precondition for adding one.

## ADR-4 · Weeks as the scheduling unit, integers as the arithmetic

Rule 10 and the sample both establish at most one access-night per activity per
week, so the decision variable is a boolean per (activity, week). `access_night`
is a per-contract accounting index and is assigned after the search from the
week's load — the search only needs to guarantee that a legal assignment exists
(`nights × workfronts`), which it does as a linear constraint.

Every quantity that is compared or optimised is an integer. Work units are held
in tenths (a standard night is 10, an ECLO night 15); score weights are held in
tenths (100 × 13 rather than 100 × 1.3). No floating-point value is ever tested
for equality, and the objective CP-SAT minimises is exactly the objective the
checker recomputes from the written files.

Overrun is linear in the last access week: `overrun_days = max(0, 7·w − P)` where
`P` is a per-contract constant. No date arithmetic enters the search.

## ADR-5 · Capacity as two inequalities instead of slot variables

Per location-week the activities present must pack into at most `supply_capacity`
possession slots under the legal-mix rule. Rather than model slot assignment,
which would add a large symmetric search space, two linear inequalities
characterise the minimum slot count exactly:

```
  slots ≥ n_PM + n_PC
4·slots ≥ n_C + 4·n_PM + n_PC
```

`tests/test_core.cpp` proves these equal the greedy packer's result, and that
they are tight, for every composition up to 3 PM / 4 PC / 12 C. Slot labels
(`b1`, `b2`, …) are then assigned deterministically after the search.

## Data flow for one solve

1. Eight CSVs land in an instance directory; every one is hashed and the eight
   hashes are hashed again into `input_hash`, binding a plan to exact bytes.
2. Strict parse. Every problem is collected with file, row and field — the loader
   does not stop at the first.
3. Topology: each activity's span expands to its booked locations; closure and
   buffer zones are derived; never-same-week pairs are precomputed (spans do not
   move, so this is a static property).
4. CP-SAT builds and searches within the budget, reporting improving solutions.
5. The plan is packed into slots, access-nights are assigned, files are written
   atomically.
6. The files are **read back from disk** and validated. Only then is the run
   reported as successful.
