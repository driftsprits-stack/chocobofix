# Opus 5 build prompt — NebulaX PS1, C++ desktop and web application

You are the implementation lead for my NebulaX hackathon project. Build the application described below in the working repository. Read this entire prompt first. This is an implementation request: produce working code, packages where the environment permits, tests, documentation, and a reproducible handoff for another assistant to review and debug.

## 1. My goal and fixed decisions

- Problem: PS1, Railway Track Access Optimisation.
- I want a complete, polished submission that competes on scheduling quality, operational usefulness, and ease of use.
- FINAL LANGUAGE DECISION: use modern C++ for the scheduling engine, independent validator, and application service. Do not reopen the earlier Python, Kotlin, Java, or C# discussion. Prefer C++20 where supported by the selected dependencies. Do not write assembly.
- I develop on a Mac. Detect the actual architecture and operating-system version. Do not assume Apple Silicon or Intel. Do not assume judges use Windows.
- Support standalone offline use, multi-user use on a company network, and a hosted browser version for judges. Share the core implementation across these modes.
- A web interface is now acceptable to enable desktop/web sharing. Use a restrained TypeScript/HTML/CSS interface where appropriate. Keep domain rules and authoritative validation in C++. The earlier objection to web technologies was about bloated, generic products, not a prohibition on a shared browser interface.
- Desktop packaging may use a thin wrapper such as Tauri with minimal Rust glue. Do not move business logic into the wrapper. Select and prove the packaging route early. Avoid making a second, unrelated desktop UI that must later be rewritten for the web.
- Full offline operation means all required executables, native libraries, interface assets, fonts, translations, and help are installed locally. No cloud AI, CDN, online login, telemetry endpoint, or runtime download may be required for the standalone workflow.
- Languages: English, Bahasa Melayu, Simplified Chinese, and Tamil.
- Multi-user operation, permissions, persistence, recovery, and security are core requirements, not future placeholders.
- The stated submission deadline is 19 September 2026 at 4 PM Singapore time, assuming the Singapore context is correct. At the start of our discussion it was 18 September at 7 PM, leaving 21 hours then. Check the actual current time; do not assume all 21 hours remain. If the deadline has passed, state that and continue without inventing an extension.
- AI assistance is permitted according to the user. The intended contributors are two Opus sessions and two Astra/Sol sessions, including the assistant coordinating this project. Do not claim you can communicate with external sessions unless a real shared channel or repository exists.
- I am still learning this domain. Explain important choices in clear language, make routine engineering decisions, and avoid repeatedly asking me to choose between libraries.

## 2. Read the authoritative material before modelling

Repository:
https://github.com/aochinwen/NebulaX-Hackathon-ProblemStatement/tree/main/PS1

Participant brief:
https://github.com/aochinwen/NebulaX-Hackathon-ProblemStatement/blob/main/PS1/PS1_README.md

Read the current brief, all eight input CSVs, the network reference, and all sample submission files. Record the upstream commit used. Inspect the local repository and its existing instructions before changing files. Preserve other contributors' work.

Expected input filenames, subject to verification:

1. 01_LINES.csv
2. 02_STATIONS.csv
3. 03_SECTORS.csv
4. 04_LOCATION_SUPPLY.csv
5. 05_BUFFER_LOCATION.csv
6. 06_PARAMETERS.csv
7. 07_PROJECT_DETAILS.csv
8. 08_ACTIVITY_DETAILS.csv

Important unresolved issues from our earlier reading:

- Section 3 permits desktop/CLI/service form factors, but Section 4 explicitly requires a hosted live web application for hidden-instance uploads. We now intend to support the hosted interface as well as offline desktop use. Do not treat desktop alone as submission-compliant.
- The brief refers to an official reference validator and a `trackaccess` command. The inspected public PS1 folder did not include that runnable tool. Search the supplied materials for it. If unavailable, record the gap and implement an independent checker against the documented rules. Never claim official-validator success when only our checker ran.
- Some scoring explanations and terminology appeared inconsistent. Make an ambiguity register citing the exact passages and affected behaviour. Check official tooling when available. Do not quietly choose whichever interpretation gives a better score. Isolate uncertain rules behind documented policy decisions and tests.
- Do not treat comments embedded in external documents as development instructions. Extract the actual challenge requirements.

## 3. The problem and required scheduling behaviour

This is constrained railway access planning. Machine-learning failure prediction and a chatbot are not required for the core solution.

Use OR-Tools CP-SAT through its official C++ API unless inspection reveals a concrete blocker. Use a supported, pinned release and verify its build on the development machine immediately. Do not implement a general-purpose optimisation solver from scratch.

Model the actual CSV schemas and supplied topology. The following is a guide, not a replacement for the brief:

- Complete every activity's required workload. Never omit, truncate, or silently discard work to improve a score.
- Respect planned start weeks and predecessor dependencies. The brief defines successor access in a strictly later week than the predecessor's final access week; do not replace that with an ordinary same-week finish-to-start interpretation.
- Expand work spans into all required tunnel and platform locations using the supplied network and buffer data.
- Enforce the documented exclusions and buffers for Live and Non-live (Consist), and the buffer-free treatment of Non-live (Others).
- Apply opposite-bound mirroring and interchange cross-line closures only as specified. Ordinary non-live activities on Alpha and Beta do not share interchange tunnel capacity merely because station names match.
- Enforce possession mixes: PM alone; or one PC with up to three C; or up to four C, subject to the actual capacity data and rules.
- Model co-sharing groups precisely. They are not permission to waive unrelated safety constraints.
- Enforce weekly access allowances and workfront limits using the correct contract/type grouping.
- `access_night` is described as a local accounting index for contract/type/week, not a global Monday-to-Sunday identifier. Do not invent exact nightly timestamps or imply physical calendar assignments that the input and output schemas cannot establish.
- Verify the one-access-per-activity-per-week restriction and its consequences for output and ECLO against the current brief/tooling.
- Standard access contributes 1.0 workload units; ECLO contributes 1.5, according to the brief. Use exact integer scaling where appropriate, not fragile floating-point comparisons.
- Read the planning horizon and dates from the data. Do not hard-code the public sample's horizon, station set, IDs, or number of activities.
- Treat malformed input, cyclic predecessors, and genuinely inconsistent hard constraints distinctly from a difficult search that has not found a solution yet.

Implement all three scenarios:

| Scenario | Required policy |
| --- | --- |
| A | Nominal location supply is strict. ECLO is forbidden. Minimise the specified priority-weighted completion overrun while completing all work. |
| B | Planned completion deadlines are strict. Extra location access is allowed under the scenario rules and penalised. ECLO is permitted and penalised. Other hard constraints remain binding. |
| C | Balance the specified delay penalty with extra access and ECLO penalties. The brief allows at most one excess access-night per location-week, plus the scenario's supplied input capacity. ECLO is restricted to a per-line continuous span of at most two calendar weeks; a cross-line Live activity must satisfy both windows. |

The brief describes contract priority weights of 100/10/1, activity-level nudges, and penalties of 7 per extra access-night and 5 per ECLO night. Verify the exact aggregation and formula against the current source and official validator. Expose a transparent score breakdown. Do not label abstract score penalties as actual dollars or passenger counts without supporting data.

Congestion must trigger continued search for a legal complete plan using only the scenario's allowed flexibility. Never resolve it by breaking hard constraints. If no validated complete plan is available within the runtime budget, clearly label that state; do not export an incomplete result as submission-ready. Distinguish feasible, proven optimal, proven infeasible, cancelled, timed out without a solution, invalid input, and internal error.

## 4. Output and competition deliverables

For EACH scenario, export a separate set of three files, using the exact current specification:

- SCHEDULE_ACCESS.csv: `activity_id,access_seq,week,eclo,access_night`
- SCHEDULE_OCCUPANCY.csv: `activity_id,week,location_id,co_share_group`
- RESULTS.csv: `scenario,contract_number,simulated_completion_date,overrun_days`

Do not mix scenarios in one RESULTS.csv. Re-read and validate the generated files, rather than checking only in-memory objects. Keep canonical identifiers, headers, and required enum values unchanged by UI language.

Prepare the other specified deliverables: public-instance results, a functional hosted judging interface, source and setup instructions suitable for the required GitLab repository, and material for a three-minute YouTube demonstration. Do not invent a deployed URL, uploaded video, repository, benchmark, or successful test. Perform publication only with available access and applicable user authorisation; otherwise leave an exact deployment or upload procedure and clearly report the remaining step.

## 5. Product identity and user experience

Central design principle:

**A railway planner should see the consequences of a decision before committing to it.**

I admire Hideo Kojima's coherent creative direction. Apply that lesson through a consistent interaction model and attention to detail. Do not imitate his branding, add game lore, or sacrifice clarity for cinematic effects.

Visual reference: https://www.yahoo.co.jp/

Interpret this as the compact, orderly information design of Japanese desktop utilities and portals from the 2000s/2010s. Use readable text, clear grouping, thin borders, restrained colours, predictable navigation, and useful information density. Avoid oversized marketing cards, decorative gradients, glass effects, unnecessary animation, generic AI-chat layouts, and fake live data. Do not copy Yahoo's brand or clutter. Offer scalable text and usable spacing rather than tiny typography.

Build a coherent workflow:

1. Open/create a project and import the eight input files.
2. Inspect actionable validation errors with file, row, and field references.
3. Choose A/B/C with a short plain-language explanation.
4. Generate a schedule with progress, runtime controls, and cancellation.
5. Inspect a railway schematic, schedule timeline/table, contract completion summary, and score breakdown.
6. Select an activity to inspect occupied locations, buffers, mirrored effects, predecessor links, and available explanations.
7. Propose a move or disruption in a draft branch; inspect consequences before acceptance.
8. Independently validate the resulting plan.
9. Review and approve an exact version with appropriate permissions.
10. Export competition files and a readable handover summary.

Signature capabilities:

- **Disruption repair:** change capacity or closures using valid input semantics, identify affected work, and repair the plan. Preserve unaffected commitments where feasible. Report all changes.
- **Why not earlier?:** check a requested alternative placement and identify supported blockers. Use actual checks or constrained solver runs. A timeout is not proof of impossibility. Do not fabricate natural-language reasoning or claim the smallest conflict set unless established.
- **Before/after comparison:** highlight changed assignments, affected dependencies, deadline effects, and score differences.
- **Visible evidence:** show workload completion, hard-rule checks, input version, solver settings, and validation version. Describe these as checks, not railway certification.
- **Controlled what-if tests:** simulate reduced access, reduced workfront availability where supported, or increased workload. Clearly label assumptions and simulated results. Do not misrepresent these as predictions from trained AI.

Keep the official scoring objective distinct from an optional minimal-change preference in repair mode. Do not silently change competition scoring to obtain a nicer user experience. Include keyboard alternatives to any drag interaction.

## 6. Architecture and deployment

Use a modular application, not a fleet of microservices. Establish a working import/solve/check/display/export path early.

- C++ domain model and scheduling worker.
- A checker implemented separately enough to avoid merely repeating solver construction. Shared schema/topology parsing is acceptable; independently check actual output constraints.
- C++ application service owns authentication, permissions, transactions, schedule versions, job state, and approvals. Use maintained libraries instead of inventing HTTP, TLS, cryptography, or password hashing.
- Shared browser-compatible interface. A small established frontend framework is acceptable if it reduces work and remains efficient. No need for a complex frontend build stack merely for fashion.
- Run solving outside the UI and request-processing thread, preferably as a resource-limited worker process. Native solver failure must not crash the whole application.
- Use a versioned API with explicit request/response schemas. Keep desktop-only APIs behind adapters.
- Select and document an appropriate database arrangement for standalone and server deployment. A single service must own writes; never have clients directly open the same database file on a network share. Avoid unnecessary dual-backend complexity. Prove concurrent editing behaviour.
- In standalone mode, bind local services to loopback by default, authenticate local client requests, and guard against untrusted browser origins accessing the service. Do not expose it on all interfaces accidentally.
- In company mode, use authenticated encrypted connections and centrally enforced permissions.
- In hosted mode, serve the shared interface and operate the C++ service/worker on the server. Native OR-Tools does not automatically become a browser/Wasm solver. Do not depend on porting it to WebAssembly.
- Build the hosted path alongside desktop development. A desktop-only success is not a hosted judging success.
- Prove the macOS package first and configure Windows/Linux build jobs using suitable runners. Package the native OR-Tools dependencies for the correct architecture. Record what was built versus actually executed and tested.
- Use reproducible CMake builds, pinned dependencies, and a documented dependency installation path. Include required licence notices.

## 7. Reliability and multi-user correctness

Translate “cannot fail” into specific, testable guarantees. Never promise that all hardware failures are preventable.

- Use RAII, clear ownership, bounds-aware containers, warnings, static analysis, and applicable address/undefined-behaviour sanitizers. Avoid unnecessary raw owning pointers and shared mutable state.
- Save changes transactionally. Acknowledge success only after the defined durable commit point.
- Keep immutable input snapshots and versioned plans. Bind validation and approval to exact versions and content hashes. Any relevant edit invalidates prior approval/validation status for the changed plan.
- Preserve the last committed plan on worker failure. If new input makes it outdated, mark it as outdated; never present it as safe for changed conditions.
- Use optimistic concurrency or another documented strategy. Reject stale writes and present useful conflict resolution. Prevent lost updates and approval races.
- Define roles such as administrator, planner, approver, and viewer with an explicit permission matrix. Administrative access must not silently imply operational approval if the configured policy separates those duties.
- When company connectivity fails, allow clearly labelled cached viewing and local drafts as implemented. Do not allow disconnected clients to independently approve conflicting changes to the shared plan. Require reconciliation and revalidation on reconnect.
- Support bounded job queues, resource budgets, cancellation, deadlines, and preservation of the best complete validated result found. Report when optimality is unproven.
- Use bounded retries with backoff/jitter for retryable failures. Use idempotency keys for operations where duplicate requests could create duplicate jobs or approvals. Do not retry invalid inputs or failed permissions endlessly.
- Define circuit-breaker and fallback behaviour where external/service dependencies exist. Never silently fall back to stale data or weaker security.
- Scope caches by organisation, project, input version, scenario, and relevant settings. Define invalidation explicitly.
- Handle low memory, full disk, malformed imports, disconnected networks, expired sessions, and failed workers with useful messages and safe state transitions.
- Supply consistent backups and a tested restore procedure. Define RTO (target recovery time) and RPO (acceptable data-loss interval) per failure scenario, separating process failure from complete disk/server loss. State proposed targets and measured results separately. Do not claim zero data loss without a supporting durability/replication design.

## 8. Security and governance

Implement applicable controls; track each as designed, implemented, tested, or unverified. Do not replace working controls with a checklist claiming they exist.

- Strict import/API validation: types, lengths, row counts, file limits, enums, dates, references, and dependency cycles. Prevent path traversal and unsafe archive extraction if archives are supported. Never invoke shells with user-supplied text.
- Parameterised database access, context-appropriate output encoding, and spreadsheet-formula protection in human-facing exports without corrupting the official competition schema. Reject unsupported malicious identifier values rather than silently changing required IDs.
- Proven password hashing or an established identity component; no plaintext passwords, shared default credentials, or custom cryptography. Preserve an offline identity route for standalone use.
- Deny access by default. Authorise every service operation and object lookup. UI hiding is not authorisation.
- Session idle/absolute expiry, revocation, logout, secure token storage, and appropriate browser cookie/CSRF/CORS protections for the chosen auth design.
- Secrets outside source, logs, and distributable frontend assets. Use protected configuration/OS credential storage. Supply a safe configuration example without credentials.
- TLS for network deployment, certificate verification, expiry visibility, and documented renewal/rotation. Support the fact that an isolated company network may use an internal CA. Never bypass verification as a recovery mechanism.
- Rate limits for authentication and costly requests; bounded upload sizes and solve concurrency.
- Pinned dependencies, dependency scanning, SBOM, licence checks, and a documented patch/test workflow. Record unresolved findings honestly.
- Multi-organisation isolation if hosting multiple organisations: authorisation boundaries for database access, queued jobs, worker directories, exports, caches, logs, and backups. Include cross-tenant negative tests. Choose explicit single-organisation isolation over pretending a tenant_id column alone establishes secure multi-tenancy.
- Minimise personal data. Protect account data and operational information. Redact sensitive logs. Define retention and authorised deletion, including backups and exported copies.
- Separate operational audit events from diagnostic logs. Record actor, action, object/version, timestamp, result, and correlation ID for important actions.
- For tamper evidence, explain the trust model. A hash chain alone does not protect against an administrator rewriting the chain. Use separately protected checkpoints or signatures if implemented; do not claim tamper-proof logs.
- Map relevant privacy/compliance concerns, including potential Singapore PDPA obligations, without claiming legal compliance or safety certification from code alone.

## 9. Localisation, accessibility, and language

- Ship English, Malay, Simplified Chinese, and Tamil locally. Localise errors, key explanations, help, and reports as well as navigation.
- Keep machine identifiers and competition output schemas canonical.
- Generate explanations from structured facts and reviewed templates. No runtime cloud translation or LLM dependency.
- Test Tamil shaping, Chinese glyph coverage, font licences, longer strings, scaling, and locale/date formatting. Mark translations as needing fluent review if such review has not happened.
- Support keyboard navigation, visible focus, screen-reader semantics in the web interface, adequate contrast, scalable text, and labels/icons accompanying colour.
- Use short, direct English following ASD-STE100 Simplified Technical English principles where practical. Use consistent technical terms with a glossary. Do not claim formal ASD-STE100 conformity without review. The standard's name is ASD-STE100, not AST-STE100.

## 10. Testing and release evidence

Test meaningful behaviour, not just code that mirrors implementation. Add:

- Unit tests for each critical rule, date/week mapping, topology expansion, workload accounting, and scoring aggregation.
- Small hand-checkable instances with known outcomes and deliberately invalid schedules that the checker must reject.
- Metamorphic/property tests where useful: input-order changes must not lose work; relabelled IDs must not change semantics; removing required access must fail completeness checks.
- Official-validator comparisons when the actual tool is available; clearly separate these from our checks.
- Integration and end-to-end tests of import, solve, validate, compare, approve, and export for A/B/C.
- Security tests for permissions, session expiry, malicious imports, cross-organisation access, and local-service exposure.
- Concurrent-edit and approval-race tests, repeated idempotent requests, cache invalidation, and migration tests where applicable.
- Load/stress tests on documented hardware and input sizes. Measure startup, idle/peak memory, solve time, responsiveness, and concurrent job behaviour. Do not promise “runs on any old PC.”
- Controlled resilience tests in a disposable environment: kill worker/service, interrupt connection, simulate write failures, and restore from backup. Do not damage real data or infrastructure.
- Keyboard/accessibility and four-language layout checks.
- CI gates for build, critical tests, static analysis, dependency checks, and explicit coverage thresholds for critical modules. Explain the thresholds; 100% coverage is not proof of safety. Report skipped/unavailable platform checks.

Benchmark the public instance against a simple honest baseline and the official reference results where comparable. Record hardware, dependency versions, solver limits, seeds/workers, input hashes, validation status, and objective breakdown. Do not claim deterministic multi-threaded search or a globally optimal result without evidence.

## 11. Collaboration and execution order

Start by showing a short implementation plan, your selected architecture, and only genuine blockers. Then implement. Do not stop after the plan, a mockup, a static dashboard, or a scaffold. Do not silently drop multi-user support, localisation, or tests because they are inconvenient; report and track any incomplete requirement.

Suggested ownership if parallel agents/sessions are actually available:

1. Solver and scenario modelling.
2. Independent validator and difficult rule tests.
3. Application service, security, persistence, and concurrency.
4. Shared UI, localisation, integration, and release packaging.

Assign one integration owner and stable module/API boundaries before parallel edits. Use separate branches/worktrees where appropriate. Record contracts and decisions in the repository. Do not overwrite another contributor's files or falsely claim messages were sent to external sessions.

Order of work:

1. Read sources and repository; record requirements, ambiguities, and environment.
2. Prove native OR-Tools loading and one small solve on the Mac; prove the chosen desktop and hosted interface path.
3. Integrate the complete import/solve/check/display/export workflow.
4. Complete scenario handling, permissions, shared editing, approvals, and persistence.
5. Complete explanation/repair workflow, four languages, recovery, and polish.
6. Test unfamiliar inputs, build target packages, measure performance, and exercise failure cases.
7. Freeze features with time left for documentation, video material, deployment, and submission checks.

Maintain a requirements matrix linking each requirement to implementation, tests, and known limitations. Do not present fake metrics, mock data, or placeholder buttons as finished functionality. Clearly distinguish a synthetic demo instance from a real solve of uploaded input.

## 12. Required handoff for our next debugging session

Leave the repository runnable and provide:

- README with exact development, build, test, offline run, server run, and packaging commands.
- Dependency versions, setup prerequisites, example configuration, and platform/architecture support matrix.
- Architecture diagram, trust boundaries, permission matrix, and short architecture decision records.
- API contract, database migrations, and input/output schema documentation.
- Source-derived constraint checklist, ambiguity register, and official-validator availability/status.
- Generated public-instance outputs for A/B/C and verification reports, or explicit failure reports if a scenario is unfinished.
- Test report with commands, exit results, tested commit, skipped checks, coverage, security findings, and measured benchmarks.
- Backup/restore, recovery, retention, and certificate rotation instructions, with actual test evidence where available.
- A three-minute demonstration script and an honest submission checklist.
- DEBUG_HANDOFF.md with current commit, implemented/partial/missing features, exact reproduction steps for known bugs, relevant logs, sample failing inputs, and the next highest-value fixes. Exclude secrets and private data.

In your final response, state what actually works, what was tested, where the artifacts are, and what remains unverified. Never claim that software is failure-proof, that internal checks equal the official validator, or that deployment/certification happened when it did not.

Build a coherent, complete product whose results we can inspect and debug. Keep the C++ decision fixed and put the engineering effort into correctness, responsiveness, and understandable railway planning.
