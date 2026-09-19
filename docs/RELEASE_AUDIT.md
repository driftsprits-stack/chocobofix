# Release audit

Audit date: 19 September 2026.

This report describes the current source. It does not claim a legal certification or a production service level.

## Security and reliability

| Requirement | Status | Evidence or limit |
| --- | --- | --- |
| Input sanitisation and injection prevention | Implemented | Prepared SQL statements, strict path IDs, JSON escaping, strict CSV schemas, spreadsheet-formula escaping, 32 MiB payload limit and row limits. |
| Authentication | Implemented | PBKDF2 password hashes and random bearer sessions. Only token hashes are stored. |
| Roles and permissions | Implemented | Administrator, approver, planner and viewer checks run on the server. Approval and planning duties are separate. |
| Session expiry | Implemented | Sliding 30-minute idle limit and fixed 8-hour limit by default. Logout, disable, password change and role change revoke sessions. |
| Secrets management | Partial | No application secret is stored in Git. Cloud Run credentials must use IAM or Secret Manager. Public `VITE_` values must never contain secrets. |
| HTTPS and certificate rotation | Platform control | The `run.app` endpoint uses managed HTTPS. Google terminates TLS and manages its certificate. The app sends HSTS. |
| Rate and abuse limits | Implemented for one instance | Login throttling, per-minute write limits, bounded solve queue, concurrent-solve limit and worker resource limits. Use Cloud Armor for distributed edge limits. |
| Dependency scanning and patching | Implemented | Locked npm dependencies, `npm audit`, CodeQL, Dependabot and pinned native dependencies. |
| Multi-tenancy | Not implemented | The service supports one organisation. Project ownership isolates planners inside that organisation. |
| Personal information | Partial | The privacy page lists usernames, profile photos, files, sessions and audit events. Real operator and privacy contact details are still required. |
| Retention and deletion | Not implemented | Users can remove their photo and administrators can disable accounts. There is no enforced retention schedule or complete erasure workflow. |
| Regulatory compliance | Not certified | If the operator is in Singapore, assess the PDPA obligations for notice, consent, access, correction, protection, retention, transfer and breach notification. Obtain legal review before production use. |
| Audit trail | Implemented with a limit | Events include an actor, action, object, result, correlation ID and SHA-256 chain. Export an external checkpoint for protection against a database administrator. |
| Error handling | Implemented | API errors use clear status codes. The UI has loading, empty, success and error states. Worker failures do not stop the service. |
| Retries and idempotency | Partial | Read failures can be retried by the user. Mutations are not automatically retried after a timeout. Approvals bind to immutable hashes. General idempotency keys are not implemented. |
| Circuit breaker and fallback | Partial | Queue and concurrency limits refuse excess solves. The solver has a labelled fallback mode. There is no distributed dependency circuit breaker because the service has no runtime third-party API. |
| Concurrency | Implemented for one instance | Store mutex, transactions, WAL, project revisions, immutable versions and approval-race tests. |
| Caching | Implemented | Bounded client cache for immutable files. Sign-out and mutations invalidate related data. Private API responses use `no-store`. |
| RTO, RPO and disaster recovery | Not verified | The worker process is isolated. No durable Cloud Run database, automatic backup, restore drill, RTO or RPO exists. |
| API protection and contracts | Implemented | Same-origin writes, bearer auth, permissions, payload limits, fixed download names and documented routes. |

## Test controls

- Native unit and store tests cover scheduling rules, authentication, sessions, permissions, isolation, concurrency, approvals and the audit chain.
- HTTP integration tests cover the API and worker failure.
- Playwright tests cover real project work, mobile widths, navigation, metadata and WCAG checks.
- Frontend coverage thresholds are 80% for statements, lines and functions, and 75% for branches in the selected critical modules.
- GitHub CI runs Linux, macOS, frontend, end-to-end, coverage and dependency checks.
- CodeQL scans JavaScript and C++.
- Synthetic solver tests provide bounded stress evidence. This is not a production capacity test.

## Website review

Implemented items include:

- no document-level horizontal scroll at 320 px;
- mobile navigation and mobile schedule controls;
- clickable logos and internal links;
- favicon, route titles, descriptions, canonical links, Open Graph image, structured data, `sitemap.xml` and `robots.txt`;
- a custom 404 page and current copyright year;
- privacy, terms and cookies pages;
- form consent on sign-in;
- lazy route loading and bounded client caching;
- keyboard focus, a skip link, 44 px controls, reduced-motion support and automated WCAG checks;
- one self-hosted Work Sans weight, a paper background, square controls, hairline rules and no gradient artwork;
- responsive pages, tables and timelines;
- no analytics, advertisements, fake reviews, pricing tiers, payments, refunds or third-party embeds.

A refund page is not required because the application has no purchase or payment feature. A cookie banner is not required for the current build because it sets no tracking or advertising cookies. Reassess this if analytics or optional storage is added.

The only image assets shipped by the public client are the favicon and social preview. The interface does not ship stock or AI photographs. Confirm the source and licence of the Work Sans font and the social preview before commercial publication.

## Visual reference review

The three supplied Flyer Archive pages use strong typography, restrained metadata, large empty areas and direct content hierarchy. ChocoboFix uses those general editorial principles. It does not copy their images, text or layout.

## Information still required

Set these build values before a public production launch:

- `VITE_OPERATOR_NAME`
- `VITE_OPERATOR_COUNTRY`
- `VITE_CONTACT_EMAIL`
- the final public domain if it changes from the current Cloud Run URL

Also decide the retention period, deletion process, backup location, RTO, RPO, incident contact, uptime monitor and Cloud billing budget. Search Console verification requires access to the final domain owner account and cannot be completed from source code.

## External references

- Singapore PDPC data protection obligations: https://www.pdpc.gov.sg/overview-of-pdpa/the-legislation/personal-data-protection-act/data-protection-obligations
- Google Cloud Run HTTPS: https://cloud.google.com/run/docs/triggering/https-request
- GitHub Dependabot security updates: https://docs.github.com/en/code-security/concepts/supply-chain-security/dependabot-security-updates
