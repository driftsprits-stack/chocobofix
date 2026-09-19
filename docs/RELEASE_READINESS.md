# ChocoboFix release readiness

Date: 19 September 2026

This file states what the product does now. It does not claim certification.
The wording uses short, direct sentences. Formal ASD-STE100 certification needs
a trained reviewer.

## Release decision

The current build is suitable for a controlled demonstration and a
single-organisation pilot. Do not use it as a multi-tenant production service.
Do not redeploy a Cloud Run revision until you have copied the current database
and plan files to durable storage.

## Control status

| Control | Status | Evidence or next action |
| --- | --- | --- |
| Input validation and injection prevention | Implemented | The CSV parser validates names, columns, types, rows, identifiers, dates, file size, and row count. SQL uses prepared statements. React escapes text. Worker arguments do not use a shell. |
| Authentication | Implemented | Local password accounts use PBKDF2. The database stores token hashes, not bearer tokens. |
| Authorisation, roles, permissions | Implemented | The server checks named capabilities. Project guards return 404 for data that the user cannot view. Browser controls are not authoritative. |
| Session expiry | Implemented | Sessions have idle and absolute expiry. Logout, account disable, and role change revoke sessions. |
| Secrets management | Partial | No application secret is stored in source. Public first-user setup is disabled. Supply `CHOCOBOFIX_BOOTSTRAP_ADMIN` from Google Secret Manager for the first start, then remove that secret from the service configuration. Do not put secrets in `VITE_` variables. |
| HTTPS and certificate renewal | Deployment control | Cloud Run serves HTTPS. A Google-managed custom-domain certificate renews automatically. A load balancer is the preferred custom-domain option. Do not expose the container port directly. |
| Rate limiting and abuse prevention | Partial | Login limits, per-IP POST limits, upload limits, a bounded queue, process limits, and solve limits exist. Add Cloud Armor or another distributed edge limit for more than one instance. |
| Dependency scanning and patching | Implemented in repository | CI runs `npm audit`. Dependabot configuration covers npm, Actions, and Docker. A maintainer must review and merge updates. |
| Multi-tenancy and data isolation | Not implemented | Project isolation exists inside one organisation. Administrators and approvers can view all projects. Use one deployment per organisation until every table and query has a verified tenant boundary. |
| PII handling | Partial | The privacy page names stored data. Data export, correction, consent withdrawal, and deletion workflows are not complete. Collect the minimum data. |
| Retention and deletion | Not implemented | Set approved periods for accounts, sessions, uploads, plans, audit records, logs, and backups. Add scheduled deletion and a legal-hold process. |
| Regulatory compliance | Not certified | A Singapore operator must review PDPA duties, name a DPO contact, record purposes, and define breach response. Obtain legal review before production use. |
| Audit trail | Implemented | The store records actor, action, object, result, correlation ID, and time for security and planning actions. |
| Tamper-evident audit | Not implemented | Database rows can be changed by a privileged database operator. Add a hash chain and send signed checkpoints to separately controlled immutable storage. Test verification and recovery. |
| Error handling | Implemented, improved | API failures use bounded messages. Worker failures now distinguish fork, setup, `exec`, runtime-loader, exit-code, and signal failures. Worker output is bounded and keeps a final partial line. |
| Retry, backoff, idempotency | Partial | Safe client reads have request deduplication and bounded caching. Mutations are not retried. Add server idempotency keys before automatic mutation retry. No payment or subscription path exists. |
| Circuit breakers and fallback | Not applicable to current integrations | The application has no remote business API. Add a circuit breaker if a remote dependency becomes part of the request path. Solver fallback plans are marked as not submission-ready. |
| Concurrency and races | Partial, tested | Worker concurrency, queue size, stale approvals, session cache isolation, and optimistic updates have tests. Add production load and thread-sanitizer runs. |
| Cache and invalidation | Implemented in client | The cache is bounded and immutable. Login-session changes cancel requests and clear cached data. Mutations invalidate affected data. API replies use `no-store`. |
| Integration, regression, end-to-end | Implemented | Native, store, integration, browser, and independent validation tests exist. CI must require them before merge. |
| Load, stress, chaos | Partial | Concurrent worker tests exist. Production-scale load, dependency failure, disk-full, restart, and restore tests still need recorded results. |
| Coverage thresholds | Implemented for named client modules | The client gate reports 94.73% statements and 88.59% branches for the transport and schedule modules. This is not whole-product coverage. |
| RTO and RPO | Not measured | Set targets, then run a timed restore drill. Do not publish an RTO or RPO before a successful drill. |
| Disaster recovery | Partial | An offline backup and restore script exists in the full repository. Cloud Run local storage is not durable. Use a durable database and object store before production use. |
| Accessibility | Implemented baseline | The client has landmarks, skip links, visible focus, keyboard forms, responsive navigation, reduced motion, text alternatives, and automated checks. Complete physical-device and screen-reader tests. |
| API contracts and protection | Implemented baseline | The same-origin API uses bearer sessions, server-side role checks, request limits, security headers, origin checks, private caching, and documented error states. Add an OpenAPI contract if third parties will use the API. |

## Website checklist

The current client includes these items:

- mobile navigation and responsive page layouts;
- horizontal-overflow checks at a 320-pixel viewport;
- favicon, route titles, route descriptions, Open Graph image, 404 page;
- working internal navigation and legacy-route redirects;
- privacy, terms, and browser-storage pages;
- a dynamic copyright year;
- loading, empty, success, and error states;
- keyboard focus, labels, contrast checks, and decorative-image handling;
- WebP social media artwork, with PNG retained for crawler compatibility;
- robots and sitemap generation;
- no analytics, advertisements, payments, testimonials, or external embeds.

The application does not need a refund policy because it does not take payment.
It does not need a tracking-cookie banner because it does not set tracking
cookies. Add consent controls before you add analytics, advertising, or optional
tracking.

The public build still needs these real values:

```text
VITE_SITE_URL=https://your-real-domain.example
VITE_OPERATOR_NAME=your legal operator name
VITE_OPERATOR_COUNTRY=Singapore
VITE_CONTACT_EMAIL=your monitored contact address
```

The build rejects a non-HTTPS site URL. It emits absolute canonical URLs,
schema markup, Open Graph URLs, and sitemap entries only when `VITE_SITE_URL`
is set. A telephone link is not present because no real telephone number was
provided. Search Console verification is an owner action after deployment.

## Production gates

Complete these gates in this order:

1. Back up the live database and plan directory. Verify the backup on another host.
2. Move state from the Cloud Run file system to a supported durable design.
3. Add and test tenant boundaries, or keep one organisation per deployment.
4. Configure the real site URL, operator, DPO contact, and retention periods.
5. Add tamper-evident audit checkpoints in separately controlled storage.
6. Configure edge rate limits, monitoring, alerts, log retention, and spending budgets.
7. Run load, restart, disk-full, dependency-failure, and timed restore tests.
8. Require CI, review, and branch protection before merge.
9. Run a keyboard, screen-reader, mobile-device, and contrast review.
10. Record the release image digest and the test results.

## GitHub hardening

For the default branch, enable a ruleset that requires a pull request, one
approval, required CI checks, resolved conversations, and a linear history.
Block force pushes and branch deletion. Enable two-factor authentication for
the owner. Enable Dependabot alerts, secret scanning, push protection, and
private vulnerability reporting where the account plan supports them. Review
deploy keys, webhooks, installed GitHub Apps, Actions secrets, personal access
tokens, collaborators, and outside collaborators.

AI tools do not need repository collaborator access to author local code. Remove
an account or GitHub App when it no longer needs access. An author name in commit
history is not active access. Do not rewrite shared history only to remove an
author from the contributors graph.

## References

- Singapore PDPC, data protection obligations: https://www.pdpc.gov.sg/overview-of-pdpa/the-legislation/personal-data-protection-act/data-protection-obligations
- Google Cloud, Cloud Run custom domains and managed certificates: https://cloud.google.com/run/docs/mapping-custom-domains
- GitHub, Dependabot quickstart: https://docs.github.com/code-security/getting-started/dependabot-quickstart-guide
- GitHub, outside collaborator removal: https://docs.github.com/organizations/managing-user-access-to-your-organizations-repositories/managing-outside-collaborators/removing-an-outside-collaborator-from-an-organization-repository
