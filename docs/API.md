# HTTP API (v1)

Base path `/api/v1`. Responses are JSON except the competition file routes, which
return `text/csv`.

**Authentication.** Every route except `/health` and the first-run `/bootstrap`
requires `Authorization: Bearer <session token>`, obtained from
`POST /auth/login`. Sessions carry an idle expiry (default 30 minutes, sliding)
and an absolute expiry (default 8 hours, fixed), and are revoked immediately when
an account is disabled or its role changes.

**Authorisation** is decided server-side from the stored role on every request.
Nothing the client sends influences it. The matrix is `RoleHas` in
`src/service/store.cpp`; the two deliberate gaps are that an **administrator
cannot approve a plan** and an **approver cannot create one**.

| Capability | viewer | planner | approver | administrator |
| --- | :-: | :-: | :-: | :-: |
| View a project you can see | ✓ | ✓ | ✓ | ✓ |
| Create project / upload instance / run solve | | ✓ | | ✓ |
| Approve or revoke a plan | | | ✓ | |
| Manage accounts | | | | ✓ |
| Read the audit trail | | | ✓ | ✓ |

**Project visibility.** A project and the data uploaded to it are visible to its
owner, plus approvers and administrators. Anyone else receives `404`, not `403`,
so the response does not confirm that the project exists.

**Correlation.** Send `X-Correlation-Id` (≤64 chars, `[A-Za-z0-9_-]`) and it is
recorded against every audit event the request produces. One is generated if you
do not send one.

Numeric ids are used for projects, instances and plan versions. Job ids are
`[a-z0-9]{1,40}`. Anything outside those shapes is rejected before it reaches the
filesystem.

---

## Setup and sessions

### `GET /health`
No authentication.
```json
{"status":"ok","queued":0,"running":1,"needs_bootstrap":false,"time":"2026-09-19T01:02:03Z"}
```

### `POST /bootstrap?username=&password=`
Creates the first administrator. **409** once any account exists, so it cannot
mint a second one later.

### `POST /auth/login?username=&password=`
```json
{"token":"…","user":{"id":1,"username":"plan.pat","role":"planner",
 "can":{"create_project":true,"run_solve":true,"approve":false,
        "manage_users":false,"view_audit":false}},
 "idle_seconds":1800,"absolute_seconds":28800}
```
`401` for a wrong password, an unknown account, or a disabled account — the same
message for all three. `429` after 10 failures for one username within 15 minutes.

The `can` block is what the interface uses to decide which controls to show. It
is a convenience, not the enforcement: the server checks again on every call.

### `POST /auth/logout` · `GET /auth/me`

## Accounts — administrator only

### `GET /users` · `POST /users?username=&password=&role=`
`role` is `viewer`, `planner`, `approver` or `administrator`. Passwords must be
at least 12 characters.

### `POST /users/{id}/role?role=` · `POST /users/{id}/disable?disabled=1`
Both end every live session for that account, so capabilities cannot go stale
mid-session. You cannot disable your own account.

## Projects

### `GET /projects` · `POST /projects?name=`
Listing returns only projects you may see. Creating requires `planner` or
`administrator`, and returns `{"id":…,"name":…,"revision":1}`.

### `POST /projects/{id}/instances`
`multipart/form-data`, exactly eight parts named for the eight canonical
filenames. 32 MiB total, 500 000 rows per file.

Success:
```json
{"instance_id":7,"accepted":true,"invalidated_plans":2,
 "summary":{"activities":54,"contracts":14,"locations":76,"horizon_weeks":30,
            "horizon_start":"2027-01-04","input_hash":"52af39d3…",
            "exclusive_pairs":58,"total_accesses":192}}
```
`invalidated_plans` counts plans that were approved or drafted against a
*different* input and are therefore no longer current. They are marked
`invalidated`, not deleted, and cannot be re-approved without a fresh run.

Rejected (`422`) — every problem is reported, not just the first:
```json
{"accepted":false,"error_count":2,
 "errors":[{"file":"06_PARAMETERS.csv","row":2,"field":"horizon_start",
            "message":"expected a date as YYYY-MM-DD, found \"2027-13-45\""}]}
```

### `POST /projects/{id}/instances/demo`
Copies the bundled public instance into the project, when the service was started
with `--public-instance`. Never presented as an upload.

### `GET /projects/{id}/instances` · `GET /instances/{id}/detail`
`detail` returns the network, contracts, and every activity with its expanded
`occupied` set and its closure zone — what the schematic and the activity panel
render from.

## Solving

### `POST /projects/{id}/jobs?instance_id=&scenario=&seconds=&expected_revision=&fallback=&strict_buffers=`
`scenario` is `A`, `B`, `C` or `all`. `seconds` is clamped to `--max-seconds`.

`expected_revision` is optimistic concurrency: state the project revision you
were working from. If it is stale you get `409` with both values, rather than
racing another planner:
```json
{"error":"this project changed while you were working on it",
 "your_revision":3,"current_revision":5}
```

`fallback=1` prices the scenario's own policy instead of enforcing it when that
policy is unsatisfiable — every physical safety rule stays hard. The resulting
version is marked `is_fallback` and **cannot be approved**.
`strict_buffers=1` uses the stricter reading of rule 6 (`docs/DERIVED_RULES.md` R6c).

Returns the job record. `429` when the queue is full (32).

### `GET /jobs/{id}` · `GET /jobs/{id}/log` · `POST /jobs/{id}/cancel`
```json
{"job_id":"…","project_id":4,"scenario":"all","state":"done",
 "version_ids":[11,12,13],"progress":["…"],"error":""}
```
`state` ∈ `queued` · `running` · `done` · `failed` · `cancelled`. Cancelling
keeps the best complete plan already written.

## Plan versions

A version is immutable and numbered per project and scenario. Editing produces a
new version; nothing is rewritten in place.

### `GET /projects/{id}/versions`
```json
{"revision":5,"versions":[
 {"id":11,"scenario":"A","version_no":1,"created_by":"plan.pat",
  "feasible":true,"violations":0,"objective":32.2,
  "content_hash":"a866…","validation_hash":"3c1b…","input_hash":"52af…",
  "is_fallback":false,"strict_buffers":false,"status":"draft",
  "approved_by":"","approved_at":"",
  "approvable":true,"not_approvable_because":""}]}
```
`status` ∈ `draft` · `approved` · `superseded` · `invalidated`.
`not_approvable_because` is filled in whenever `approvable` is false, so the
interface can explain rather than silently disable a control.

### `GET /versions/{id}/validation` · `GET /versions/{id}/files/{FILE}`
`FILE` must be `SCHEDULE_ACCESS.csv`, `SCHEDULE_OCCUPANCY.csv` or `RESULTS.csv`.
Any other name is `404`, including names that exist on disk.

The validation report carries `"checker_is_official_validator": false`, so no
consumer can mistake it for the organisers' validator output.

### `POST /versions/{id}/approve?content_hash=&validation_hash=` — approver only
Both hashes must match what the approver was shown. A mismatch means the plan
changed underneath them and the approval is refused with `409`.

Refused, at the store, regardless of role or endpoint:

| Condition | Message |
| --- | --- |
| hard violations | `this plan has N hard violations and cannot be approved` |
| fallback mode | `…breaches its scenario's own policy; it is not submission-ready` |
| input replaced | `…produced from an input that is no longer current` |
| already approved | `this plan version is already approved` |
| a concurrent approver won | `this plan version was approved by someone else a moment ago` |

Approving a newer version marks the previously approved one `superseded`, so only
one plan per scenario ever stands approved.

### `POST /versions/{id}/revoke?reason=` — approver only
Returns the version to `draft` and records the revocation with its reason. The
approval history is kept.

## Analysis

### `POST /instances/{id}/explain?activity=&week=&scenario=&seconds=`
"Why not earlier?" Returns the worker's output, which says one of three things
and never conflates them: yes (with the cascade it causes), proven impossible
(naming the binding rule), or not established within the budget.

### `POST /instances/{id}/repair?scenario=&seconds=&supply=LOC@WEEK=N&supply=…`
Applies a disruption in the input's own semantics and re-plans, preferring to
keep unaffected commitments. Up to 40 overrides.

## Audit — approver or administrator

### `GET /projects/{id}/audit`
```json
{"events":[{"ts":"2026-09-19T01:02:03Z","actor":"appr.avi","action":"plan.approve",
            "object":"plan_version 11","result":"ok","correlation_id":"ui-4f2a",
            "detail":"scenario A v1 content a866191497"}]}
```
Actions recorded: `auth.login`, `auth.logout`, `authz.deny`, `bootstrap`,
`user.create`, `user.role`, `user.disable`, `user.enable`, `project.create`,
`instance.upload`, `job.create`, `job.cancel`, `plan.create`, `plan.approve`,
`plan.revoke`, `plan.explain`, `plan.repair`. Refusals and denials are recorded
as well as successes.

**The audit table is ordinary rows.** An administrator with database access could
alter them. No tamper-evidence is claimed, and a hash chain alone would not
provide it either.

---

## Errors

| Status | Meaning |
| --- | --- |
| 400 | malformed request (unknown scenario or role, wrong file count, bad supply spec) |
| 401 | no session, or an expired, revoked or unknown token |
| 403 | authenticated, but your role does not permit this |
| 404 | unknown id, or a resource you may not see — the two are deliberately indistinguishable |
| 409 | conflict: stale revision, hash mismatch, or an approval that cannot stand |
| 422 | the instance parsed but breaks the schema — the body carries every problem |
| 429 | login throttled, or the solve queue is full |

## Versioning

The path carries the version. A breaking change to a response shape takes a new
path prefix; fields may be added to existing responses without one.
