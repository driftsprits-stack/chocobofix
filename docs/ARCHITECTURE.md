# Architecture

## System shape

ChocoboFix is one web service with a separate solver process.

```text
browser
  │ HTTPS (Google Cloud Run)
  ▼
trackaccess-service
  ├─ React static files
  ├─ authentication and role checks
  ├─ SQLite store
  ├─ upload and request limits
  └─ bounded job queue
       │ fork + exec, CPU/memory/file limits
       ▼
trackaccess solver
  ├─ strict CSV import
  ├─ OR-Tools CP-SAT model
  ├─ CSV export
  └─ independent result check
```

Cloud Run terminates TLS. The container receives HTTP from the Cloud Run proxy. The service does not hold a TLS private key.

## Trust boundaries

| Boundary | Data | Control |
| --- | --- | --- |
| Browser to service | credentials, CSV files, API commands | same-origin checks, bearer session, role check, request limit, payload limit |
| Service to SQLite | users, projects, plans, audit events | prepared statements, transactions, foreign keys, one process lock |
| Service to solver | one instance directory and bounded options | separate process, queue limit, CPU, memory and file limits |
| Service to browser | JSON, CSV and static files | JSON escaping, fixed download names, CSP and other security headers |

## Access model

The deployment is one organisation. A planner sees projects that they own. An administrator and an approver can see all projects in that deployment. This supports internal review. It does not isolate separate customer organisations.

The roles are administrator, approver, planner, and viewer. The server owns the permission matrix. The client does not grant access. An administrator cannot approve a plan. An approver cannot generate one.

## Storage model

SQLite stores accounts, session hashes, project metadata, assignments, approvals, photos, and audit events. Uploaded instances and solver output files use directories below the configured service root.

The store uses WAL mode, full synchronous commits, foreign keys, a busy timeout, and a process mutex. Project revisions provide optimistic concurrency checks. Immutable plans use content hashes.

The current Cloud Run root is ephemeral. Use a managed SQL database and object storage before production use or before more than one Cloud Run instance is allowed.

## Audit design

Each new audit row contains the hash of the previous event and a hash of its own canonical fields. `Store::VerifyAuditChain` recalculates the chain. A changed or removed row causes verification to fail.

This is tamper-evident inside the application boundary. It is not an external proof. A privileged database operator can rewrite the complete chain. Export signed checkpoints to an append-only service for production use.

## Failure handling

The service rejects excess writes with HTTP 429 and a retry time. It rejects a solve when the queue is full. The client bounds concurrent requests, cancels requests at sign-out, applies a timeout, and does not retry mutations automatically. This prevents duplicate writes after an uncertain response.

A solver crash fails one job. It does not stop the web service. Generated files are checked before a successful version is stored.

## Architecture decisions

### ADR-1: Keep the solver in a separate process

A process boundary contains native crashes and resource exhaustion. The same CLI code runs locally and in the service.

### ADR-2: Keep an independent checker

The checker reads the generated files again. It does not reuse the solver constraints. This can expose a solver-model error.

### ADR-3: Use SQLite for the demonstration

SQLite gives transactions, indexes, foreign keys, and simple backup tools. It fits one service instance. It does not fit a multi-instance or multi-region production deployment.

### ADR-4: Pin native dependencies

OR-Tools and cpp-httplib use fixed versions. CI and the final container build use the same versions.

### ADR-5: Use integer scheduling arithmetic

The solver stores work and score values as integers. It does not use floating-point equality for constraints or validation.
