# Security policy

## Reporting a vulnerability

Do not open a public issue for a vulnerability that could expose credentials,
uploaded project data, or a running deployment. Contact the repository owner
privately through the account that publishes this repository. Add a dedicated
security contact here before public deployment.

Include the affected version, a concise reproduction, impact, and any suggested
mitigation. Do not include real passwords, session tokens, or private planning
files.

## Supported state

This project is under active development. Security fixes apply to the current
default branch. No long-term support releases are published.

## Deployment boundaries

The service binds to loopback by default and does not terminate TLS itself. A
public deployment must use an HTTPS reverse proxy, restrict the data directory,
and keep secrets outside the repository.

The current design supports one organisation per installation. Project-level
authorisation does not provide isolation between separate customer
organisations. Audit records are ordinary database rows and are not
tamper-evident.

See [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) for deployment checks and
[docs/REQUIREMENTS_MATRIX.md](docs/REQUIREMENTS_MATRIX.md) for known gaps.
