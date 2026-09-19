# Test report

Test date: 19 September 2026.

## Current results

| Check | Result |
| --- | --- |
| Store, authentication, role, concurrency and audit-chain tests | 104 passed, 0 failed |
| CLI and HTTP integration suite | 181 passed, 0 failed |
| Frontend unit tests | 48 passed, 0 failed |
| Frontend statement coverage | 93.75% |
| Frontend branch coverage | 87.41% |
| Frontend function coverage | 94.82% |
| Frontend line coverage | 97.03% |
| npm vulnerability audit at moderate level | 0 vulnerabilities |
| Production frontend build | Passed |
| Canonical, Open Graph, structured data, sitemap and robots generation | Passed with the live Cloud Run URL |

The local browser runner could not start Chromium in the Codex macOS sandbox because macOS denied its Mach rendezvous service. GitHub CI installs Chromium on Ubuntu and runs the same seven Playwright tests. Those tests cover real project creation, schedules, keyboard dialog use, 320 px overflow, route metadata, mobile navigation, same-origin API protection and automated WCAG checks.

## Integration coverage

The integration suite verifies:

- reproducible scenario output;
- all required files and exact headers;
- independent validation and Python cross-checks;
- malformed inputs and corrupted outputs;
- infeasibility, timeout and fallback labels;
- disruption repair and plan comparison;
- authentication, session failures, roles and project isolation;
- approval hashes and approval races;
- audit isolation and hash chaining;
- safe file downloads and path rejection;
- single-page routes and missing assets;
- concurrent solver requests;
- service survival after a worker process is killed.

## Limits

These results are evidence for the tested build. They are not a production load test, a disaster-recovery drill, a penetration test, or a legal certification. See `RELEASE_AUDIT.md` for the remaining production work.
