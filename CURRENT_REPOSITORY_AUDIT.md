# ChocoboFix current-repository audit

Audit date: 19 September 2026

Source reviewed: `84115937-e4cf-437b-b014-ff805535894e.zip`

Archive SHA-256:
`812363d562d43aff9d570cceefd50ccad48625bf774e749c9281fc599f6270a0`

## Verdict

The committed source contains the worker-runtime repair and the rewritten
README. The code is a strong hackathon build, but the supplied ZIP and branch
state are not ready to publish without the corrections in this package.

## Material findings

1. The ZIP is incomplete. It contains only `src/service/main.cpp`; 16 other
   tracked source files are absent. The embedded Git objects still contain the
   complete commit, but an ordinary extraction looks like a broken repository.
2. The ZIP contains `.git/`, `.claude/`, macOS metadata, `node_modules/`, native
   build products, downloaded dependencies, generated outputs, and a live
   SQLite database with WAL and shared-memory files. Do not publish or submit
   this ZIP.
3. The remote named `origin` points to
   `/Users/muhammedfahdabdallah/Documents/Project_Hackathon`. Pushing to it does
   not update GitHub. The actual GitHub remote is named `github`.
4. The embedded GitHub reference remains on `master` at commit `cb6e6e8`. The
   worker repair and README commits are on local branches. They still need to be
   pushed to the `github` remote and merged.
5. The worker-repair commit accidentally deleted 11 existing documentation
   files and the CodeQL configuration. This correction restores them.
6. CI and CodeQL watched `main`, while this repository uses `master`. This
   correction watches both names and restores the maintained CodeQL v4 matrix.
7. The Docker and Google Cloud ignore files did not exclude every common secret
   format. This correction excludes environment files, private keys, database
   sidecars, logs, and service-account JSON files from build contexts.
8. No high-risk credential pattern was found in the tracked current commit or
   reachable embedded history. This is a pattern scan, not proof that no secret
   exists.

## Verification completed

- 48 frontend tests passed from a clean dependency install.
- Configured coverage measured 93.75% statements and 87.41% branches.
- The production frontend build passed.
- `npm audit` reported zero known vulnerabilities at the time of the audit.
- Workflow YAML parsed successfully.
- The worker, Docker, CMake, and service-main repair files match the previously
  relocated Linux solver check that generated valid A, B, and C smoke plans.
- The final Docker image was not built in this audit environment. GitHub CI must
  complete that check before deployment.

## Apply the corrections

Start from the local repository, not from the incomplete ZIP:

```bash
cd "/Users/muhammedfahdabdallah/Documents/penis-and-co/chocobofix"
git status --short
git remote -v
git switch rewrite-readme
```

If `git status --short` shows unrelated work, commit or stash it before copying
the correction package into the repository.

After the corrected files are in place:

```bash
git add .github .gitignore .dockerignore .gcloudignore \
  APPLY_AND_DEPLOY.md SECURITY.md docs CURRENT_REPOSITORY_AUDIT.md

git diff --cached --check
git commit -m "Preserve project docs and correct repository release controls"
git push -u github rewrite-readme
```

Create a pull request from `rewrite-readme` into `master`. Confirm the native,
macOS, frontend, browser, final-container, dependency-review, and CodeQL checks
before merge.

## Deployment stop condition

Do not deploy a replacement Cloud Run revision until the current SQLite
database, uploads, and generated plans have a verified backup. The default
container stores them under `/var/lib/trackaccess`, which is not durable across
Cloud Run instance or revision replacement.

After CI passes, deploy a candidate revision without moving all traffic. Use
the browser to upload a new eight-file dataset, generate A, B, and C, reopen the
stored versions, download them, and validate every submission before moving
traffic.
