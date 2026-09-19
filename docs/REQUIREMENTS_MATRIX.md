# Requirements matrix

This file covers the functional PS1 solution. The security, website, legal and operations checklist is in `RELEASE_AUDIT.md`.

| Requirement | Status | Evidence |
| --- | --- | --- |
| Read the eight PS1 CSV files | Implemented | Strict importer, schema checks and aggregated row errors in `src/core`. |
| Generate scenarios A, B and C | Implemented | CP-SAT solver and HTTP job flow. |
| Export the three required CSV files | Implemented | Deterministic export and fixed download names. |
| Validate generated output | Implemented | Independent C++ validator and separate Python cross-check. |
| Explain an infeasible week | Implemented | CLI explanation names the binding rule when it proves infeasibility. |
| Repair after a supply disruption | Implemented | Repair mode limits schedule movement and validates the result. |
| Compare plans | Implemented | Version metadata and schedule views expose changed outcomes. |
| Accounts and roles | Implemented | Administrator, approver, planner and viewer roles. |
| Project isolation | Implemented | A planner cannot list or open another planner's project. |
| Approval workflow | Implemented | Approval binds to content and validation hashes. Changed input invalidates old approval. |
| Audit trail | Implemented | Correlation IDs and a SHA-256 event chain. |
| Responsive web interface | Implemented | React routes, mobile navigation, table and timeline views, 320 px overflow checks. |
| Accessibility | Implemented with ongoing review | Semantic forms, focus handling, reduced motion, keyboard dialog and automated WCAG checks. |
| Durable production storage | Not implemented | The current Cloud Run container uses ephemeral SQLite and files. |
| Multi-organisation tenancy | Not implemented | One deployment supports one organisation. |

## Verification

- `test_core` checks scheduling and validation rules.
- `test_store` checks accounts, sessions, roles, isolation, approvals, concurrency and audit chaining.
- `tests/integration.sh` checks CLI output, HTTP contracts, worker containment and concurrent use.
- Vitest checks critical client data and API code with enforced coverage thresholds.
- Playwright checks the complete browser flow, small screens and accessibility in GitHub CI.
