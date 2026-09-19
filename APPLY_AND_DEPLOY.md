# Apply and deploy the ChocoboFix repair

This repair fixes the generic `worker process could not be started` failure.
It does not modify a live database. Do not deploy until you have a verified
copy of the current database and plan files.

## 1. Confirm the source

Use `https://github.com/driftsprits-stack/chocobofix` as the repository. Before
you apply this repair, record the deployed Cloud Run revision and image digest:

```sh
gcloud run services describe chocobofix \
  --region asia-southeast1 \
  --format='yaml(status.latestReadyRevisionName,status.url,spec.template.spec.containers[0].image)'
```

Compare the checked-out commit with the commit used for that image. Keep the
old revision available for rollback.

## 2. Back up state first

The current container command stores state under `/var/lib/trackaccess`.
Cloud Run container storage is temporary. A new revision does not inherit that
directory. Export the current SQLite database, uploads, and generated plans
before deployment. Verify that the backup opens and that its hashes match.

If the live revision contains the only copy of these files, stop here. Move the
state to a durable design before you deploy. Do not mount a SQLite database on
Cloud Storage FUSE.

## 3. Review and commit

Create a repair branch:

```sh
git switch -c fix/worker-runtime
git status --short
git diff --check
```

Review the files in this package. Then commit them:

```sh
git add CMakeLists.txt Dockerfile src/service/main.cpp .gitignore .github docs APPLY_AND_DEPLOY.md
git commit -m "Fix solver runtime packaging and worker diagnostics"
git remote -v
git push -u github fix/worker-runtime
```

In the current local repository, `github` is the remote for
`https://github.com/driftsprits-stack/chocobofix.git`. The remote named
`origin` points to another local folder and does not publish to GitHub. Open a
pull request into `master`. Require the CI checks to pass before merge. Do not commit
`.env` files, passwords, tokens, databases, private keys, or live backups.

## 4. Configure build values

Set these values to real information. Do not invent them:

```text
VITE_SITE_URL=https://your-real-service-or-domain
VITE_OPERATOR_NAME=your legal operator name
VITE_OPERATOR_COUNTRY=Singapore
VITE_CONTACT_EMAIL=your monitored contact address
```

Supply the first administrator through Google Secret Manager as
`CHOCOBOFIX_BOOTSTRAP_ADMIN`. Remove the secret from the service configuration
after the first account exists. Never put a secret in a `VITE_` variable.

## 5. Verify the final image

The Docker build now checks the copied runtime image. It fails if a shared
library is missing or if the relocated solver cannot generate A, B, and C.

```sh
docker build --pull -t chocobofix:worker-fix .
docker run --rm --entrypoint sh chocobofix:worker-fix -c \
  'ldd ./build/trackaccess && ./build/trackaccess solve --data ./data/smoke --out /tmp/smoke --scenario all --seconds 15 --workers 1'
```

The output must contain no `not found` entry. The command must exit with code
zero and create validated A, B, and C plans.

## 6. Deploy a candidate revision

Deploy without sending all traffic to the new revision first. Use the actual
project and service settings for your environment. Do not change the service
port to 6969; Cloud Run supplies `PORT`, and the image uses it automatically.

After deployment, inspect the candidate logs. A worker failure now includes
the launch stage, operating-system error, signal, exit status, or loader error.

## 7. Run the release check

Against the candidate URL:

1. Sign in with a non-admin planner account.
2. Upload a new eight-file PS1 dataset.
3. Generate all policies.
4. Confirm A, B, and C reach a completed state.
5. Open each schedule and confirm its validation state.
6. Download each submission and validate it independently.
7. Confirm the version history records all three plans.
8. Confirm a second account can see only projects allowed by its role.
9. Check mobile navigation, keyboard focus, page titles, legal links, and 404.
10. Confirm the old revision and the verified backup can restore service.

Only move traffic after these checks pass. Record the image digest, revision,
test results, backup location, and rollback owner.

## 8. Controls that still need operator work

The repository cannot configure every production control by itself. Enable a
GitHub branch ruleset, required review and CI, two-factor authentication,
secret scanning, push protection, monitoring, alerts, spending budgets, log
retention, and a durable state service. Set retention, deletion, RTO, and RPO
only after the owner approves them and a restore drill measures them.

See `docs/RELEASE_READINESS.md` for the full control matrix.
