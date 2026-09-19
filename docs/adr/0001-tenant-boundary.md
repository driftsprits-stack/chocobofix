# ADR 0001: Keep one organisation per deployment

Status: accepted for the current release

## Context

The data model has user and project ownership. It has no organisation or tenant
identifier. Administrators and approvers can view all projects in one store.

## Decision

Run one organisation in each deployment. Do not advertise the current service
as multi-tenant.

## Consequences

Project guards protect users inside one organisation. They do not isolate two
customers. A future multi-tenant release must add a tenant identifier to users,
projects, instances, jobs, plans, approvals, audits, file paths, cache keys, and
every query. Tests must try cross-tenant reads and writes on every endpoint.
