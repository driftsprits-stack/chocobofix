# Security policy

## Report a vulnerability

Do not open a public issue for a vulnerability that can expose credentials or project data. Contact the repository owner through the GitHub account that publishes this repository. Add a public security email before production use.

Include the affected commit, the steps to reproduce the issue, the impact, and a proposed fix. Do not send real passwords, session tokens, or private railway files.

## Supported version

Security fixes apply to the current `master` branch. The project has no long-term support release.

## Controls in this repository

- The service uses prepared SQLite statements and strict input limits.
- Passwords use PBKDF2-HMAC-SHA256 with a unique salt.
- Only session token hashes are stored.
- Sessions have idle and absolute expiry times.
- The server checks roles and project access on every protected route.
- Login, write, queue, upload, CPU, memory, and output limits are bounded.
- The browser rejects cross-site writes. The service sends CSP, HSTS, frame, MIME, referrer, opener, and resource-policy headers.
- Audit events contain a SHA-256 chain. The service can verify this chain.
- The repository defines tests, coverage gates, `npm audit`, CodeQL, dependency review, and Dependabot updates. Repository owners must enable Actions, secret scanning, push protection, and required checks in GitHub settings.

## Limits

This release supports one organisation per deployment. Project ownership isolates planners inside that organisation. It is not multi-tenant software.

The Cloud Run deployment stores SQLite data and uploaded files on the container file system. A new revision starts with empty data. This is acceptable for the hackathon demonstration. It is not durable production storage.

The audit hash chain detects an unexpected changed or missing event. A database administrator can rewrite the rows and recompute the chain. Export the final hash to an external append-only log for stronger evidence.

The application has no payment, refund, analytics, advertising, or third-party embed feature.

See `docs/RELEASE_AUDIT.md` for the complete status and `docs/DEPLOYMENT.md` for deployment controls.
