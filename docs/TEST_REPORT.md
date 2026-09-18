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
| Rules, counterexamples, mutations, metamorphic | `./build/test_core` | **90 passed, 0 failed** |
| Accounts, roles, approvals, concurrency | `./build/test_store` | **94 passed, 0 failed** |
| Sample corroboration | `./build/check_sample` | **sample accepted, 0 hard violations** |
| Integration / API / security | `./tests/integration.sh build` | **121 passed, 0 failed** |
| Shared-project layer over HTTPS | `tests/test_multiuser.py https://…` | **58 passed, 0 failed** |
| ASan + UBSan | `./build-asan/test_core`, `./build-asan/check_sample` | **clean, no reports** |

**305 automated checks in total.**

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

## 4a. Explain, repair and compare

| Check | Result |
| --- | --- |
| `explain A004 --week 21` (reachable) | **YES**, objective unchanged at 32.2, 1 activity-week moved |
| `explain A004 --week 16` (reachable, cascading) | **YES**, objective unchanged, 6 activity-weeks moved across A003, A004, A007 |
| `explain A004 --week 5` (impossible) | **proven impossible**; binding rule identified as predecessor precedence |
| `explain` with a 1 s budget | reports **not established**, never a bare "no" |
| `repair` closing `SEC:BET:H01_H02:EB` in weeks 15–16 | objective 32.2 → **41.3** (+9.1), overrun 28 → 35 d, 14 activity-weeks churn, repaired plan **feasible** |
| Unknown location in a disruption | refused, exit 1 |
| Competition objective after a repair | recomputed from the written plan; the churn preference does not enter it |

The cascade in the second row is the point of the feature: moving A004 into week
16 requires A003 to finish by week 15, which in turn displaces A007. The tool
shows that before anything is committed.

## 4b. Unsatisfiable scenario policy

An instance was constructed by pulling every `planned_completion_date` back to
2027-03-21, which Scenario B cannot meet.

| Check | Result |
| --- | --- |
| `solve --scenario B` (default) | **infeasible**, nothing exported |
| `solve --scenario B --fallback` | plan exported, run prints `OUT OF POLICY`, `NOT_SUBMISSION_READY.txt` written |
| Breaches in the fallback plan | **13, all `planned_date`** — no closure, buffer, capacity, mix, allocation, workfront or precedence breach |
| Levers spent before accepting lateness | 60 ECLO nights and 1 excess access-night |

That the breach set is exactly `{planned_date}` is the property that matters: the
fallback relaxes the scenario's policy and nothing else.

## 4c. Shared-project layer

`./build/test_store` (94 checks) and `tests/test_multiuser.py` (58 over HTTP,
58 over HTTPS) cover the parts that decide whether a plan can be trusted.

| Property | Result |
| --- | --- |
| Password hashing | PBKDF2-HMAC-SHA256 via the platform (CommonCrypto here), 210 000 iterations, per-user salt; the password never appears in the stored record, and the same password hashes differently each time |
| **An administrator cannot approve a plan** | refused, 403 |
| **An approver cannot create a plan** | refused, 403 |
| **A planner cannot approve their own work** | refused, 403 |
| Approval with a wrong content hash | refused, 409 |
| Approval with a wrong validation hash | refused, 409 |
| Approval of a plan with hard violations | refused at the store, whatever the role |
| Approval of a fallback plan | refused at the store, whatever the role |
| Double approval of one version | refused, 409 |
| Approving a newer version | supersedes the older one |
| Replacing a project's input | marks plans built on the old input `invalidated`; they cannot be re-approved |
| Stale project revision on a solve | refused, 409, reporting both revisions |
| Another planner reading someone else's project | **404, not 403** — existence is not confirmed |
| Another planner solving in it, or reading its dataset | 404 |
| It appearing in their project list | absent |
| Session revoked on logout / disable / role change | stops working immediately |
| Absolute session expiry | enforced even when the idle window is wide open |
| Audit coverage | `auth.login`, `authz.deny`, `user.create`, `project.create`, `instance.upload`, `job.create`, `plan.create`, `plan.approve` (both `ok` and `refused`) |

## 4d. Hosted deployment shape

Tested on 2026-09-18 with the service on loopback and a TLS terminator in front
(`deploy/tls_proxy.py`, written for the test because neither Caddy nor nginx was
installed).

| Check | Result |
| --- | --- |
| `GET /api/v1/health` over HTTPS | 200, TLS 1.2+ negotiated |
| Browser interface served over HTTPS | served |
| Security headers survive the proxy | CSP, `nosniff`, `DENY`, `no-referrer` present |
| **Full shared-project suite over HTTPS** | **58 passed, 0 failed** |
| Plain HTTP against the TLS port | refused |
| Certificate expiry readable for monitoring | `notAfter` reported by `openssl s_client` |

Certificate verification was never disabled; the throwaway CA was supplied
through `TA_CA`, which is the same mechanism an internal-CA deployment uses.

**Not tested:** `deploy/Dockerfile` (never built — no container runtime),
`deploy/Caddyfile`, `deploy/nginx.conf`, `deploy/trackaccess.service` (written,
not run), and automatic certificate issuance.

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
- **No Linux or Windows build was produced or executed.** Only arm64 macOS
  15.6.1. `scripts/fetch_deps.sh` selects a Linux OR-Tools asset and the CMake
  configuration is portable, but that path is **unverified**.
- **The container image has never been built.**
- **Nothing is deployed and no URL exists.**
- **No backup restore was rehearsed**, so no RTO or RPO is measured.
- **No multi-user, approval-race or cross-tenant test exists**, because accounts,
  approvals and tenancy are not implemented.
- **Tamil and Chinese glyph rendering was not verified** beyond the development
  machine's font set.
- The worker-crash test kills the worker while the public instance solves in
  under a second, so the kill sometimes lands after the solve has completed. The
  test accepts either outcome and always asserts the service survives. On the
  final run the kill did land mid-solve and the job was correctly reported
  `failed`, but this is timing-dependent rather than deterministic.
