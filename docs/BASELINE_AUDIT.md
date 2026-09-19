# Baseline audit

**Audited tree:** `master` @ `2b7e569` ("Redesign the interface: light editorial
workspace"), cloned from `Documents/Project_Hackathon` on 2026-09-19.
**Machine:** Apple M4 Pro, macOS 24.6.0, Apple clang 17.0.0, CMake 4.4.3,
OR-Tools 9.14.6206 (pinned binary, arm64).

Everything below was produced by running the command shown in this tree today.
No figure is carried over from `docs/TEST_REPORT.md`.

> **Note on the brief's "known starting points."** §1 of the implementation brief
> describes an older archive: overlapping `chocobofix.css` / `workbench.css`,
> a `docs/FIREBASE_PLAN.md`, vanilla JS over multiple stylesheets. None of that is
> in this tree. That state survives in the sibling copies
> (`chocobofix-penisCo`, `Downloads/ChocoboFix`); `master` has already moved past
> it. The audit below describes what is actually here.

---

## 1. What works, verified today

| Check | Command | Result |
| --- | --- | --- |
| Build | `cmake --build build -j8` | clean, 0 warnings surfaced |
| Rules / counterexamples / mutations | `./build/test_core` | **120 passed, 0 failed** |
| Accounts, roles, approvals, concurrency | `./build/test_store` | **94 passed, 0 failed** |
| Sample corroboration | `./build/check_sample` | **accepted, 0 hard violations** |
| Public instance A/B/C | `./build/trackaccess solve --scenario all` | all three **OPTIMAL (proven)**, 0 hard violations on re-check |

Public-instance scores reproduced today:

| Scenario | Status | Overrun days | Excess nights | ECLO | Objective |
| --- | --- | --- | --- | --- | --- |
| A | OPTIMAL (proven) | 28 | 0 | 0 | 32.2 |
| B | OPTIMAL (proven) | 0 | 0 | 6 | 30.0 |
| C | OPTIMAL (proven) | 14 | 0 | 2 | 26.1 |

Total solve wall time for all three: ~0.35 s. Exports carry the exact required
headers for all three files.

**The service runs and serves the interface.** `trackaccess-service --host
127.0.0.1 --port 8137 --root … --web web` returns 200 with CSP (no
`unsafe-eval`), `nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer`
and no CORS headers.

## 2. Reusable, and worth keeping

1. **`src/core` + `src/solver` + `src/validator`.** Correct, fast, proven optimal
   on the public instance, with an independent checker that re-derives rules from
   the emitted files rather than the solver's model (ADR-2). This is the most
   valuable asset in the repository.
2. **`docs/DERIVED_RULES.md`.** An evidence-based ambiguity register with honest
   status labels, and an explicit statement that the official validator is absent
   from upstream and that two agreeing implementations prove fidelity, not
   correctness. This is exactly the posture §3 of the brief demands.
3. **Disruption repair already exists.** `src/solver/solver.h` carries
   `SupplyOverride`; the CLI takes `--supply LOCATION_ID@WEEK=SUPPLY`, refuses to
   run without a disruption, minimises churn, and records the disruption in the
   plan's provenance. This is the signature interaction and it is already
   load-bearing rather than a mock.
4. **`src/service` + `src/store`.** Accounts, roles, versions, approvals and audit,
   with 94 passing tests over exactly that surface.
5. **Journey skeleton in `web/app.js`:** sign-in → projects → wizard
   (upload → check → generate → review) → overview / schedule / activities /
   history / settings, plus an "adjust (disruption repair)" section.

## 3. Defects found

**D1 — Scenario C exports are not reproducible.** Re-running scenario C with
`--workers 8` twice produces **72 differing rows** in `SCHEDULE_ACCESS.csv` at an
identical objective (26.1), identical overrun (14), identical ECLO (2). CP-SAT
picks a different optimum per run. `--workers 1` is deterministic across runs.

Consequence: the committed `out/public/C/*` no longer matches a fresh run, and a
published provenance hash cannot be reproduced by a judge. A and B reproduce
byte-identically. **Fix required:** a deterministic publish path (fixed seed and
single worker, or a lexicographic tie-break applied before export) so that
published artefacts and their hashes are stable.

**D2 — Documentation is stale against the code.** `docs/TEST_REPORT.md` reports
`test_core` as "90 passed"; it is **120** today. It also reports totals ("305
automated checks") that include suites not re-run here. The committed
`out/public/C` predates the current code. Brief §10 requires public outputs
regenerated from the *final* code — these must be regenerated and the report
rewritten from measured runs.

**D3 — The art direction in §4 is not implemented.** `web/app.css` is a coherent
system, but a *different* one. Conflicts with explicit requirements:

| §4 requires | `web/app.css` has |
| --- | --- |
| one flat ground, warm ivory `#F4F1E8` | `--bg: #f4f6f5` (cool grey-green) |
| one accent `#173C83` for everything | `--accent: #0d5f61` (teal) |
| no separate success/error palettes | `--ok` / `--warn` / `--bad` triplets |
| no rounded corners, no shadows | `--radius: 6px`, `--shadow-1`, `--shadow-2` |
| one grotesque sans, Work Sans 400, self-hosted | system sans **plus** a serif display stack |
| display headings ~112–160px | page titles far smaller |

**D4 — No editorial introduction.** The application opens directly on sign-in.
Brief step 1 requires a public introduction carrying "open workspace →" and
"try sample →". "Try sample project" exists, but behind the sign-in screen, which
also blocks priority 3 (a visitor understanding the product in under a minute).

## 4. Missing infrastructure

- No `package.json`, no Vite, no React, no build step anywhere in the tree.
- No Firebase of any kind: no Auth, no Firestore, no rules, no emulator config,
  no `.env.example`. §6 mandates all of these.
- No terms/privacy pages, and no consent record bound to UID + terms version +
  server timestamp.
- No browser tests (Playwright), no automated accessibility checks, no load or
  resilience harness.

## 5. Rule ambiguities carried forward

`DERIVED_RULES.md` marks R1–R5 resolved against the shipped sample, R6a settled,
and **R6b (which works a buffer pushes) "adopted, contested"** — the brief is
self-contradictory there and the sample does not discriminate. A `--strict` mode
exists for the alternative reading and `out/public-strict/` holds its outputs.
This stays open, stays labelled, and must not be presented as certified.

The official validator is **not** in upstream. No artefact in this project may
describe our checker's verdict as an official-validator result.

---

## ADR-4 · Firebase for identity; the existing store stays authoritative

§6 mandates Firebase Auth and Firestore, and separately says to "choose one
authoritative store for schedule/job records after auditing the existing store;
document that decision and avoid unsynchronised dual writes." Having audited it:

- **Identity, profiles, preferences, memberships and versioned terms acceptance
  move to Firebase** (Auth + Firestore), verified server-side with the Admin SDK.
- **Schedule, job, version, approval and audit records stay in the existing
  store**, which already has 94 passing tests over that behaviour. Moving them to
  Firestore would trade tested correctness for conformance to a default, and
  would invite exactly the unsynchronised dual writes §6 forbids.
- **The native solver is retained**, as §6 expressly permits and this audit
  supports: proven optimal, sub-second, independently re-checked.

Consequence: a Node gateway holds the Admin SDK, verifies ID tokens (verification,
not decoding), and is the only thing that talks to the native service. The native
service is never exposed to the Internet.
