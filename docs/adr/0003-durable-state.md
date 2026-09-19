# ADR 0003: Do not treat Cloud Run local storage as durable

Status: required before production release

## Context

The service stores accounts, sessions, uploads, plans, approvals, and audits
under one local root. Cloud Run instance storage can disappear when an instance
stops or a revision changes. SQLite also needs file-system locking semantics
that a general object store does not provide.

## Decision

Do not redeploy a stateful revision until the current data has a verified
backup. For a controlled single-host installation, use a persistent disk and a
single writer. For a managed multi-instance service, migrate relational records
to a supported database and artifacts to object storage. Do not place the
SQLite file on Cloud Storage FUSE.

## Consequences

A Cloud Run demo can use temporary data. It cannot promise durable accounts or
plans. The production migration needs a tested export, import, rollback, and
timed recovery procedure.
