# Requirements matrix

Status vocabulary — used strictly:

- **Implemented** — built, and covered by a test that would fail if it broke.
- **Partial** — built, with a named gap.
- **Designed** — written down, not built.
- **Not implemented** — absent. No partial credit claimed.

Nothing in this table is marked implemented on the strength of code existing; the
Evidence column names the test or artefact that demonstrates it.

## Competition deliverables (PS1 §4)

| # | Deliverable | Status | Evidence / gap |
| --- | --- | --- | --- |
| 1 | Public-instance results for A/B/C | **Implemented** | `out/public/{A,B,C}/` — all three feasible, proven optimal |
| 2 | Hosted live web app for hidden instances | **Partial** | The application is built and runs (`trackaccess-service` + `web/`); upload→solve→validate→export is exercised end to end by `tests/integration.sh`. **It is not deployed and no URL exists.** See `docs/DEPLOYMENT.md`. |
| 3 | 3-minute YouTube video | **Not implemented** | Script written: `docs/DEMO_SCRIPT.md`. Not recorded, not uploaded. |
| 4 | GitLab repository | **Partial** | Repository is complete and committed locally with full history and setup instructions. **Not pushed** — no GitLab remote or credentials available. |

## Scheduling rules (PS1 §2.4)

| Rule | Status | Evidence |
| --- | --- | --- |
| 1 Workload conservation (ECLO = 1.5 units) | Implemented | `test_core` workload mutation; exact tenths arithmetic |
| 2 Planned start week | Implemented | `test_core` planned_start mutation |
| 3 Predecessor precedence, strictly later week | Implemented | `test_core` precedence mutation; cycles rejected at load |
| 4 Closures, buffers, Live mirroring, interchange crossing | Implemented | `docs/DERIVED_RULES.md` R6; sample accepted with 0 violations |
| 5 Possession capacity and legal mixes | Implemented | `test_core` mix mutation; packing inequalities proven tight |
| 6 Co-sharing exemption | Implemented | derived and corroborated against the sample (R2/R6) |
| 7 Weekly allocation | Implemented | `test_core` allocation mutation |
| 8 Workfronts | Implemented | enforced as `nights × workfronts`; checked in the validator |
| 9 ECLO yield | Implemented | 15 tenths vs 10; workload test |
| 10 ECLO continuity window (Scenario C) | Implemented | per-line window variables in the solver; validator re-checks |

## Scenarios

| | Status | Public-instance result |
| --- | --- | --- |
| A — strict supply, no ECLO | Implemented | feasible, **proven optimal**, objective 32.2 |
| B — strict planned dates | Implemented | feasible, **proven optimal**, objective 30.0, zero overrun |
| C — balanced, ≤1 excess night per location-week | Implemented | feasible, **proven optimal**, objective 26.1 |
| Distinct outcome states (optimal / feasible / infeasible / timeout / cancelled / invalid input / internal error) | Implemented | `SolveStatus`; a timeout is never reported as infeasibility |
| Infeasibility attribution | Implemented | `trackaccess diagnose` lifts one rule group at a time and names which binds |
| No strictly redundant access-night | Implemented | the solver forbids an access that could be dropped while still meeting the workload, so no plan squats on a possession slot it does not need |

## Product and interface (§5)

| Requirement | Status | Note |
| --- | --- | --- |
| Import with actionable file/row/field errors | Implemented | shown in the Validation panel and over the API |
| Scenario choice with plain-language explanation | Implemented | in all four languages |
| Progress, runtime control, cancellation | Implemented | live log; cancel signals the worker; best complete plan kept |
| Schedule timeline, contract summary, score breakdown | Implemented | Schedule tab |
| Railway schematic | Implemented | Network tab, per-week occupancy against supply |
| Activity inspection: locations, buffer, predecessors | Implemented | Activity detail panel; selection highlights the schematic |
| Independent validation in the UI | Implemented | Check tab, with the not-the-official-validator caveat on screen |
| Export competition files | Implemented | Export tab; bytes served are the bytes validated |
| Keyboard alternatives, visible focus, scalable text | Implemented | no drag-only interaction; table rows are focusable |
| **Disruption repair** | **Implemented** | `trackaccess repair`, and the Repair tab. Supply is changed in the input's own semantics; the re-plan keeps unaffected commitments and reports churn. Covered by `tests/integration.sh`. |
| **"Why not earlier?"** | **Implemented** | `trackaccess explain`, and the activity detail panel. Answers yes / proven-impossible / not-established, never collapsing the last two. Three integration checks, including that a one-second budget does not produce a bare "no". |
| **Before/after comparison** | **Implemented** | `trackaccess compare`, and the repair output, which diffs activity weeks and the objective. |
| **What-if tests** | **Partial** | Reduced access is supported (it is the same mechanism as repair). Increased workload and reduced workfront availability are **not** exposed. |
| Repair preference kept out of the competition score | **Implemented** | The churn term is a solver tie-break only; `SolveResult::objective_tenths` is recomputed from the plan. Asserted in `tests/integration.sh`. |
| Four languages | Partial | All strings ship in en/ms/zh/ta. **ms, zh and ta are unreviewed by a fluent speaker** and are marked as such in `web/i18n.js`. Tamil and Chinese rendering depends on system fonts; not tested across platforms. |

## Reliability and multi-user (§7)

| Requirement | Status | Note |
| --- | --- | --- |
| Atomic durable writes | Implemented | temp → `fsync` → `rename`; success reported only after |
| Immutable input snapshots, content hashes | Implemented | `input_hash` over the eight files, carried into every report |
| Worker isolation and resource bounds | Implemented | separate process, `RLIMIT_AS`/`CPU`/`FSIZE`; kill-the-worker test |
| Bounded queue, cancellation, budgets | Implemented | queue cap 32, concurrent solves capped, per-job time budget |
| Single writer owns the store | Implemented | clients never touch store files |
| RAII, no raw owning pointers, sanitizers | Implemented | ASan + UBSan clean on the core and checker |
| **User accounts, roles, permission matrix** | **Not implemented** | Auth is a single shared bearer token. There are no users, so there are no roles. |
| **Approvals and approval races** | **Not implemented** | — |
| **Plan versioning, optimistic concurrency, stale-write rejection** | **Not implemented** | — |
| **Offline/reconnect reconciliation** | **Not implemented** | — |
| **Backup, restore, RTO/RPO** | Designed | `docs/DEPLOYMENT.md` describes the procedure. **No restore has been tested, and no RTO/RPO has been measured.** No durability claim is made. |
| Retries with backoff, idempotency keys | Not implemented | The queue is bounded and jobs are not auto-retried. |
| Circuit breakers | Not applicable | No external service dependency exists. |

## Security (§8)

| Control | Status | Note |
| --- | --- | --- |
| Strict import validation (types, ranges, enums, references, cycles, row/size caps) | Implemented | `test_core` input-rejection group |
| Path traversal and identifier injection refused | Implemented | ids restricted to `[a-z0-9]`; crafted location ids rejected; `tests/integration.sh` |
| No shell invoked with user text | Implemented | worker started with `execv` and an argument vector, never a shell |
| Spreadsheet formula neutralisation | Implemented | available in `CsvWriter`; **deliberately off for competition files**, where it would corrupt the required schema |
| Constant-time token comparison | Implemented | service |
| Upload size and row caps, bounded solve concurrency | Implemented | 32 MiB, 500k rows, queue cap |
| Security headers, no CORS exposure | Implemented | CSP, `nosniff`, `DENY`, `no-referrer` |
| Secrets outside source and logs | Implemented | token is generated at start or passed in; never written to the store |
| **Password hashing / identity component** | **Not applicable** | No passwords exist, because no accounts exist. |
| **TLS** | **Not implemented** | Terminate in front of the service. `docs/DEPLOYMENT.md`. No certificate handling in-process. |
| **Rate limiting on authentication** | **Not implemented** | Only the queue cap bounds work. |
| **Multi-organisation isolation** | **Not implemented** | Explicitly single-tenant. No `tenant_id` exists, and none is pretended. |
| **Audit events separate from diagnostic logs** | **Not implemented** | — |
| SBOM / dependency scanning | Partial | Two dependencies, both pinned with recorded SHA-256 and licences listed in the README. No automated scanner runs. |
| PDPA / compliance mapping | Not implemented | The application stores no personal data: no accounts, no names, no contact details. No compliance claim is made. |

## Testing (§10)

| | Status | Evidence |
| --- | --- | --- |
| Unit tests for critical rules | Implemented | 77 checks in `test_core` |
| Mutation tests the checker must reject | Implemented | 9 mutations, each breaking one rule |
| Metamorphic tests | Partial | Row-order invariance implemented. **ID relabelling invariance is not implemented.** |
| Official-validator comparison | **Impossible** | The reference validator is not published in the problem repository. Corroboration is against the shipped sample instead, and every report says so. |
| Integration / end-to-end | Implemented | 57 checks in `tests/integration.sh` |
| Security tests | Implemented | auth, traversal, filename restriction, unknown ids |
| Concurrency / approval races | Not implemented | No approvals exist to race. |
| Load and stress | Partial | Measured on the public instance and on synthetic multiples; see `docs/TEST_REPORT.md` |
| Resilience (kill worker) | Implemented | `tests/integration.sh` |
| Sanitizers | Implemented | ASan + UBSan clean |
| Accessibility / four-language layout | Partial | Keyboard and focus verified in-browser; **Tamil and Chinese glyph rendering not verified across platforms** |
| CI gates, coverage thresholds | **Not implemented** | No CI pipeline is configured. No coverage figure is measured, so none is quoted. |
