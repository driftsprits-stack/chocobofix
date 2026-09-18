# Test report

**Tested commit:** see `git rev-parse HEAD` (this report was produced from the
working tree at the time of the final public run).
**Instance:** PS1 public instance, upstream commit `966c976`,
`input_hash = 52af39d328d856b597dab64b3e51e6ac40969c2aedd6a9888deed2f896f95e96`.

**Hardware:** Apple M4 Pro, 24 GB RAM, 14 cores.
**Software:** macOS 15.6.1 (24G90), Apple clang 17.0.0, CMake 4.4.3,
OR-Tools 9.14.6206 (pinned binary distribution, arm64).

Every figure below was produced by running the command shown. Nothing is
estimated or carried over from a previous run.

---

## 1. Suite results

| Suite | Command | Result |
| --- | --- | --- |
| Unit / mutation / metamorphic | `./build/test_core` | **77 passed, 0 failed** |
| Sample corroboration | `./build/check_sample` | **sample accepted, 0 hard violations** |
| Integration / API / security | `./tests/integration.sh build` | **46 passed, 0 failed** |
| ASan + UBSan | `./build-asan/test_core`, `./build-asan/check_sample` | **clean, no reports** |

The mutation group is the load-bearing part: nine deliberately broken schedules,
each violating one rule, which the checker must reject — a dropped access-night,
work before its planned start week, ECLO under Scenario A, an out-of-range
`access_night`, an over-filled location, a PM sharing a slot, a successor in its
predecessor's own week, a duplicate weekly access, and a Scenario B overrun. All
nine are rejected with the expected rule tag.

## 2. Public-instance results

```
./build/trackaccess solve --data data/upstream/PS1/01_data \
    --out out/public --scenario all --seconds 120 --workers 8
```

| Scenario | Status | Solver wall | Overrun days | Extra nights | ECLO | Objective | Peak RSS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A | **OPTIMAL** (proven) | 0.057 s | 28 | 0 | 0 | **32.2** | 60 MB |
| B | **OPTIMAL** (proven) | 0.038 s | 0 | 0 | 6 | **30.0** | 46 MB |
| C | **OPTIMAL** (proven) | 0.227 s | 14 | 0 | 2 | **26.1** | 104 MB |

Total wall time for all three including I/O and re-validation: **0.48 s**.

All three were re-read from disk and re-checked after export: 0 hard violations.

### Baseline comparison

The upstream `03_submission_sample/` (Scenario A) scored by the same checker:

| | Overrun days | Contracts overrunning | Weighted score |
| --- | --- | --- | --- |
| Upstream sample | 28 | 3 | **48.3** |
| This tool, Scenario A | 28 | 3 | **32.2** |

Same total overrun, 33% lower weighted penalty: the overrun is placed on
lower-tier contracts and lower-priority activities. This is a comparison against
the shipped sample under **our** checker. It is **not** a comparison against the
official reference validator or the organisers' reference solver, neither of
which is available.

### Independent cross-check

A second implementation of the rules, written in Python from the brief and
sharing no code with the C++ (`tools/derive/crosscheck.py`), was run on all four
submissions:

| Submission | C++ checker | Python cross-check | Agreement |
| --- | --- | --- | --- |
| upstream sample | feasible, 48.3 | feasible, 48.3 | yes |
| ours, A | feasible, 32.2 | feasible, 32.2 | yes |
| ours, B | feasible, 30.0 | feasible, 30.0 | yes |
| ours, C | feasible, 26.1 | feasible, 26.1 | yes |

## 3. Determinism and seed sensitivity

| Check | Result |
| --- | --- |
| Same seed, two runs, SHA-256 of `SCHEDULE_ACCESS.csv` | identical (`656aad75344cb13e…`) |
| Seeds 1, 7, 42 (Scenario A) | objective 32.2 in all three, all feasible |

Because all three scenarios are solved to **proven optimality**, the objective is
seed-independent here. No claim is made that a multi-threaded search would be
bit-identical on an instance that only reaches a feasible-but-unproven result;
in that case the seed and worker count do matter, and both are recorded in the
run output.

## 4. Load and stress

Synthetic instances produced by `tools/derive/gen_stress.py`, which replicates the
demand book onto the same fixed network. **These are synthetic, not competition
instances, and their results say nothing about scoring.**

| Instance | Activities | Contracts | Result | Wall | Peak RSS |
| --- | --- | --- | --- | --- | --- |
| public | 54 | 14 | optimal | 0.06 s | 60 MB |
| 2× | 108 | 28 | **proven infeasible** | 90 s | 122 MB |
| 3× | 162 | 42 | **proven infeasible** | 0.3 s | 123 MB |
| 5× | 270 | 70 | **proven infeasible** | 0.4 s | 187 MB |

The multiples are genuinely infeasible, not a solver failure: doubling the demand
book while holding the network, the horizon and the planned start dates fixed
overruns what the network can supply. `trackaccess diagnose` attributes it:

```
$ ./build/trackaccess diagnose --data /tmp/stress2 --scenario A --seconds 90
  all rules enforced                        infeasible
  lift: closure/buffer exclusivity          infeasible
  lift: location possession capacity        optimal
  lift: contract weekly nights x workfronts infeasible
  lift: predecessor precedence              infeasible
  lift: planned start weeks                 optimal
```

Note what this rules out: lifting our **closure/buffer** interpretation alone does
**not** restore feasibility. The conservative reading documented in
`docs/DERIVED_RULES.md` R6 is therefore not what limits density on these
instances — location capacity interacting with planned start weeks is.

## 5. Service behaviour

| Measurement | Result |
| --- | --- |
| Idle resident memory | 1.8 MB |
| Resident memory after 6 jobs | 3.0 MB |
| 6 jobs submitted simultaneously | 6/6 completed within 12 s, queue drained |
| Worker killed mid-solve (`pkill -9`) | service stayed healthy; job reported failed, not left running |
| Startup to first request served | < 2 s |

Solve concurrency is capped (default 2) and the queue is bounded (32); a 33rd
queued job is refused with 429 rather than accepted and dropped.

## 6. Security checks

All in `tests/integration.sh`:

| Check | Result |
| --- | --- |
| Unauthenticated request to a protected route | 401 |
| Wrong bearer token | 401 |
| Traversal-styled job id (`..%2f..%2fetc%2fpasswd`) | 404, does not resolve |
| Identifier outside `[a-z0-9]` | 404 |
| Non-competition filename in the files route | 404 |
| Unknown scenario / unknown instance | 400 / 404 |
| Crafted location id (`../../etc/passwd`) in an uploaded CSV | rejected at load |
| Predecessor cycle in an uploaded CSV | rejected, cycle named |
| Malformed date, missing file, unknown contract reference | rejected with file/row/field |

## 7. Not tested — stated plainly

- **The official reference validator was never run**, because it is not published
  in the problem repository. Every corroboration here is against the shipped
  sample submission using our own checker.
- **No CI pipeline exists.** No coverage figure is measured, so none is quoted.
- **No Windows or Linux build was produced or executed.** Only arm64 macOS 15.6.1
  was built and tested. The CMake configuration is portable and
  `scripts/fetch_deps.sh` selects a Linux asset, but that path is **unverified**.
- **No backup restore was rehearsed**, so no RTO or RPO is measured.
- **No multi-user, approval-race or cross-tenant test exists**, because accounts,
  approvals and tenancy are not implemented.
- **Tamil and Chinese glyph rendering was not verified** beyond the development
  machine's font set.
- The worker-crash test kills the worker while the public instance solves in
  under a second, so the kill often lands after the solve completes. The service's
  survival is verified; the mid-solve kill path is only weakly exercised.
