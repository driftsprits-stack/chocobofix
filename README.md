# ChocoboFix

**Rail maintenance plans that people can inspect before they approve.**

ChocoboFix is our solution to NebulaX Hackathon Problem Statement 1. It converts
eight railway-planning CSV files into feasible maintenance schedules for three
operating policies. The application then explains the result, shows it on a
shared timeline, checks every exported file, and records who generated or
approved each version.

This repository contains the complete product:

- a C++20 scheduling engine built with Google OR-Tools CP-SAT;
- an independent schedule checker;
- a native HTTP service with SQLite storage;
- a React interface for planners, approvers, administrators, and viewers;
- command-line tools for validation and submission export;
- automated native, integration, frontend, and browser tests.

> ChocoboFix supports planning decisions. A plan marked **validated** has passed
> this repository's checker. It is not an official railway operating approval.

## The PS1 problem, in plain language

Rail maintenance teams cannot close any track whenever they want. An activity
may depend on earlier work, require several locations at once, consume limited
weekly access, block a nearby workfront, or need a safety buffer around its
closure zone.

PS1 supplies eight CSV files describing:

- lines, stations, sectors, and railway locations;
- the access available at each location and week;
- locations that must be buffered together;
- project contracts and planned completion dates;
- maintenance activities, durations, priorities, predecessors, and workfronts;
- the parameters used to score a completed schedule.

ChocoboFix assigns every activity to one or more weeks while enforcing the
physical and sequencing rules. It then writes the three files required by the
PS1 validator:

```text
SCHEDULE_ACCESS.csv
SCHEDULE_OCCUPANCY.csv
RESULTS.csv
```

## Three policies, three questions

Every project produces all three PS1 policies. Their objectives differ, so
their scores should not be compared as though they were one competition.

| Policy | Question answered | What it prioritises |
| --- | --- | --- |
| **A · Fixed access** | What can we complete with the normal access supply? | No extra access and no ECLO; completion may move later. |
| **B · Fixed deadlines** | What access is required to meet the planned dates? | Deadlines remain fixed; extra access or ECLO may be required. |
| **C · Balanced** | What is the best compromise? | Trades limited additional access against delay. |

Safety, topology, location occupancy, precedence, buffer, closure-zone, and
workfront rules remain constraints in every policy.

## How the application works

1. **Create a project.** Give the planning exercise a name and owner.
2. **Upload the eight CSV files.** ChocoboFix checks filenames, columns, types,
   references, dates, limits, and topology before scheduling begins.
3. **Generate plans.** The service runs A, B, and C as isolated worker jobs.
4. **Review the result.** The schedule page shows activities by week, project,
   contract, and coordinator. Status and timing are visible without opening a
   raw CSV file.
5. **Inspect the reason.** Activity details show the locations, access type,
   predecessors, and constraints behind the selected week.
6. **Approve a version.** Approvals are tied to the exact plan and validation
   result that the approver reviewed.
7. **Export the submission.** Each policy downloads as the three CSV files
   expected by the PS1 validator.

The global **Schedules** area keeps schedules separate from project setup. A
planner can filter and compare work across projects without entering every
project individually.

## What makes ChocoboFix different

### The export is the source of truth

After solving, ChocoboFix writes the CSV files, reads them again, and validates
the written result. The interface does not report success from a separate
in-memory representation that could disagree with the submitted files.

### Solving and checking are separate

The solver searches for a plan. The checker independently decides whether the
export obeys the implemented rules. A solver result is not accepted merely
because the solver produced it.

### The schedule is designed for people

The interface leads with the operational result: what happens, when it happens,
where access is required, who coordinates it, and whether it finishes early,
on time, or late. Dense source data remains available, but it is not the first
thing a worker has to interpret.

### Plans have history

The service records plan versions, validation state, approvals, assignments,
and security-relevant actions. Roles are enforced by the service rather than
only by hiding buttons in the browser.

### It works without a hosted dependency

The solver, checker, service, interface, and SQLite store can run on one
machine. A network connection is needed only for the initial dependency fetch
or when the team chooses to host the service.

## Repository layout

```text
client/          React application and browser tests
src/core/        CSV parsing, domain model, topology, scoring, and export
src/solver/      CP-SAT scheduling model for policies A, B, and C
src/validator/   independent conformance checker
src/service/     HTTP API, authentication, SQLite store, and worker supervision
src/cli/         trackaccess command-line program
tests/           native, store, integration, and multi-user tests
tools/derive/    rule cross-check and stress-data utilities
data/upstream/   recorded copy of the public PS1 data
web-dist/        production frontend build
docs/            architecture decisions and release-readiness evidence
```

`web/` is the legacy interface. The active frontend is in `client/` and builds
to `web-dist/`.

## Build from a clean checkout

### macOS

Install Apple's command-line tools and CMake:

```bash
xcode-select --install
brew install cmake
```

### Ubuntu 24.04

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake curl ca-certificates \
  libsqlite3-dev libssl-dev python3
```

### Build the native programs and frontend

```bash
./scripts/fetch_deps.sh

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

npm ci --prefix client
npm run build --prefix client
```

The dependency script downloads pinned builds of OR-Tools and cpp-httplib and
verifies their SHA-256 values. They are stored under `third_party/` and are not
installed globally.

## Run ChocoboFix locally

For the first local start, provide an administrator account through an
environment variable:

```bash
export CHOCOBOFIX_BOOTSTRAP_ADMIN='admin:replace-this-with-a-long-password'

./build/trackaccess-service \
  --host 127.0.0.1 \
  --port 8080 \
  --root ./var \
  --web ./web-dist \
  --worker ./build/trackaccess \
  --public-instance ./data/upstream/PS1/01_data
```

Open <http://127.0.0.1:8080/>. After the administrator exists, remove the
variable from the current shell:

```bash
unset CHOCOBOFIX_BOOTSTRAP_ADMIN
```

The browser bootstrap route is available only when the service binds to
loopback. Do not place a bootstrap password in source code, a committed `.env`
file, a screenshot, or a `VITE_` variable.

## Generate the three policies from the command line

```bash
./build/trackaccess solve \
  --data ./data/upstream/PS1/01_data \
  --out ./out/public \
  --scenario all \
  --seconds 120 \
  --workers 8
```

Successful output is written to:

```text
out/public/A/
out/public/B/
out/public/C/
```

Each directory contains the three submission CSVs and `VALIDATION.json`.

Validate any exported policy independently:

```bash
./build/trackaccess validate \
  --data ./data/upstream/PS1/01_data \
  --submission ./out/public/A
```

The command exits with a non-zero status if the data is invalid, no plan is
produced, or the written schedule fails validation.

## Test before deployment

```bash
ctest --test-dir build --output-on-failure
./tests/integration.sh build

npm run test:coverage --prefix client
npm run build --prefix client
npm audit --prefix client --audit-level=moderate
```

The GitHub workflows also build the final Docker runtime image. The Dockerfile
checks the copied solver with `ldd` and runs a relocated A/B/C smoke test. This
is important because build-stage tests cannot detect a library that was omitted
from the final image.

## Cloud Run deployment

The root `Dockerfile` builds the frontend, native service, and final runtime
image. Cloud Run supplies the `PORT` variable automatically; keep the service
container port at `8080`.

```bash
gcloud run deploy chocobofix \
  --source . \
  --region asia-southeast1 \
  --allow-unauthenticated \
  --port 8080
```

Before deploying, read [`APPLY_AND_DEPLOY.md`](APPLY_AND_DEPLOY.md). Deploy a
candidate revision first and verify that the actual web application can create,
validate, store, and reopen A, B, and C.

### Important storage warning

The default container command stores SQLite data and generated artifacts under
`/var/lib/trackaccess`. Cloud Run's container filesystem is temporary. A new
revision or replacement instance does not provide durable application state.

Do not treat the default Cloud Run configuration as durable production storage.
Back up the database and plan files before a redeployment, and move state to a
supported persistent design before real operational use. Do not place a live
SQLite database on Cloud Storage FUSE.

## Security boundaries

The current service includes:

- prepared SQL statements and strict CSV validation;
- PBKDF2 password hashing and hashed bearer sessions;
- idle and absolute session expiry;
- server-enforced roles and project permissions;
- origin checks for browser writes;
- login, POST, upload, queue, worker, and solve limits;
- private API caching rules and session-scoped client caches;
- bounded worker logs with specific launch and loader errors;
- audit events containing actor, action, object, result, time, and correlation ID.

The current build is intended for one organisation per deployment. It is not a
finished multi-tenant service. Audit rows are useful but are not independently
tamper-evident. Automated retention, deletion, legal-hold, and complete PII
export workflows still require implementation and an approved operating policy.

See [`docs/RELEASE_READINESS.md`](docs/RELEASE_READINESS.md) for the control
matrix and known limits. Report security concerns using [`SECURITY.md`](SECURITY.md).

## Release checklist

Before presenting or publishing a new revision:

1. Confirm the Git commit used by the deployed image.
2. Run native, integration, frontend, and final-container checks.
3. Back up and verify the existing database and generated plans.
4. Generate A, B, and C through the browser, not only through the CLI.
5. Reopen each stored plan and download its submission files.
6. Validate those downloads independently.
7. Check a planner, approver, viewer, and disabled account.
8. Test navigation and schedule overflow at a 320-pixel viewport.
9. Record the image digest, revision, test results, and rollback owner.
10. Keep the previous working revision available until verification finishes.

## Current limitations

- One organisation per deployment; tenant isolation is not complete.
- Cloud Run container-local SQLite storage is not durable.
- Audit records are not protected by an external immutable checkpoint.
- RTO and RPO have not been measured in a production restore drill.
- Policy and regulatory compliance require operator and legal review.
- Translation should not be advertised until every interface string has been
  translated and reviewed by fluent speakers.

These limits are recorded so that a successful demonstration is not mistaken
for an unsupported production guarantee.

## Repository

<https://github.com/driftsprits-stack/chocobofix>

## Licence and third-party software

ChocoboFix was created for the NebulaX Hackathon PS1 submission. The public PS1
dataset remains the property of its authors.

Third-party dependencies are fetched rather than committed:

- **Google OR-Tools 9.14.6206** — Apache License 2.0;
- **cpp-httplib 0.18.3** — MIT License;
- frontend package licences are recorded by `client/package-lock.json`.

Review the dependency notices before redistributing the application outside the
hackathon context.
