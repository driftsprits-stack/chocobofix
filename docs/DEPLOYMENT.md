# Deployment

The public service runs on Google Cloud Run at
`https://chocobofix-53566346633.asia-southeast1.run.app`. This file records how
to publish a new revision and how to verify it before judges use it.

---

## What was actually tested

The hosted shape is *browser → HTTPS → reverse proxy → HTTP on loopback →
service → worker processes*. That shape was exercised end to end on the
development machine (arm64 macOS 15.6.1) on 2026-09-18:

| Step | Result |
| --- | --- |
| Service bound to `127.0.0.1:8098`, TLS proxy on `:8443` with a throwaway certificate | working |
| `GET /api/v1/health` over HTTPS | 200, TLS 1.2+ negotiated |
| Browser interface served over HTTPS | served |
| Security headers survive the proxy | CSP, `nosniff`, `DENY`, `no-referrer` all present |
| **Full shared-project suite over HTTPS** (`tests/test_multiuser.py https://localhost:8443`) | **58 checks passed** |
| Plain HTTP against the TLS port | refused |
| Certificate expiry readable for monitoring | `openssl s_client` reports `notAfter` |

The proxy used for that test is `deploy/tls_proxy.py` — a ~40-line TLS
terminator written for the test because neither Caddy nor nginx was installed.
**It is not for production** and says so in its own header. Production configs
are `deploy/Caddyfile` and `deploy/nginx.conf`.

Reproduce it:

```bash
openssl req -x509 -newkey rsa:2048 -nodes -keyout /tmp/ta-key.pem -out /tmp/ta-cert.pem \
  -days 2 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
npm ci --prefix client && npm run build --prefix client
./build/trackaccess-service --host 127.0.0.1 --port 8098 --root /tmp/depl --web ./web-dist \
  --worker ./build/trackaccess --public-instance ./data/upstream/PS1/01_data \
  --bootstrap-admin "admin.ada:administrator-pass-1" &
python3 deploy/tls_proxy.py --cert /tmp/ta-cert.pem --key /tmp/ta-key.pem \
  --listen 8443 --upstream 8098 &
TA_CA=/tmp/ta-cert.pem python3 tests/test_multiuser.py https://localhost:8443
```

## Limits that remain

- A local Linux container build was not available on the development Mac. The
  Cloud Run build uses Linux, and the final Docker image now runs the sample
  validator before it can be released.
- **`deploy/trackaccess.service`, `Caddyfile` and `nginx.conf` were written, not
  run.** Only the proxy *shape* was verified, with a different proxy.
- **No automatic certificate issuance was exercised.** The test used a
  self-signed certificate; Caddy's ACME path is untested here.
- **No restore from backup was rehearsed**, so no RTO or RPO is measured.

---

## Update the existing Cloud Run service

Use Google Cloud Shell, or any machine where `gcloud` is signed in to the Google
account that owns the service:

```bash
git clone https://github.com/driftsprits-stack/chocobofix.git
cd chocobofix
gcloud config set project YOUR_PROJECT_ID
gcloud run deploy chocobofix --source . --region asia-southeast1 \
  --allow-unauthenticated --min 1 --max 1
```

Cloud Run detects the root `Dockerfile`, builds it, and sends all traffic to the
new revision after the container starts successfully. The build fails if the
solver cannot load OR-Tools or validate the supplied sample.

Use one minimum and one maximum instance for the hackathon demo. The application
currently stores SQLite data and uploaded files on the instance filesystem. A
new revision starts with an empty store, and more than one instance can split
users and projects. Replace this storage with a managed database and object
store before using the service as a durable production system.

Judges need to sign in because an unauthenticated solver endpoint would be an
open compute service and uploaded datasets would not be protected. The intended
arrangement is:

- one `planner` account per judging panel, credentials given in the submission;
- each judge's uploads live in their own project, invisible to other planners
  (verified: another planner gets 404, not 403);
- the bundled public instance is loadable in one click, so nothing has to be
  uploaded just to see the tool work.

After a fresh deployment, the first visitor creates the first administrator.
That administrator can create planner accounts in **Settings → accounts**. To
replace a debug administrator, create a second administrator, sign in as that
account, and disable the old account from the same page.

---

## Procedure, once access exists

### 1. Build on the target host

```bash
git clone <repo> trackaccess && cd trackaccess
./scripts/fetch_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/test_core && ./build/test_store && ./build/check_sample   # refuse to deploy if these fail
```

### 2. Run the service on loopback

```bash
sudo useradd --system --home /var/lib/trackaccess --create-home trackaccess
sudo install -d -o trackaccess /var/lib/trackaccess
sudo cp deploy/trackaccess.service /etc/systemd/system/
sudo systemctl enable --now trackaccess
```

The first account is created through the interface (the sign-in screen offers
"set up the first administrator" while no account exists), or non-interactively
with `--bootstrap-admin user:password`. That flag is ignored once any account
exists, so it cannot be used to mint a second administrator later.

### 3. Terminate TLS

```bash
sudo cp deploy/Caddyfile /etc/caddy/Caddyfile   # edit the hostname first
sudo systemctl reload caddy
```

Caddy obtains and renews automatically. Verify with `caddy list-certificates`
and alert below 21 days remaining. For nginx use `deploy/nginx.conf` with
certbot; `certbot renew --dry-run` proves renewal works before it matters.

### 4. Verify the deployment

```bash
curl -s https://<host>/api/v1/health
python3 tests/test_multiuser.py https://<host>          # add TA_CA=<ca.pem> for an internal CA
```

Then open it in a browser and walk import → generate → versions → approve →
export. A deployment is finished when a judge can upload eight files and
download three, not when the health check returns 200.

### 5. Create the judging account

```bash
# as the administrator, through the Accounts tab, or:
curl -s -X POST https://<host>/api/v1/users \
  -H "Authorization: Bearer <admin session token>" \
  -d "username=judge.panel&password=<generated>&role=planner"
```

Put those credentials in the submission text, not in the repository.

---

## Operational notes

**Resource bounds.** Each solve runs as a separate process with `RLIMIT_AS`
(default 4 GB), `RLIMIT_CPU` and `RLIMIT_FSIZE` applied. Concurrent solves are
capped (`--max-solves`, default 2) and the queue is bounded (32); a 33rd job is
refused with 429 rather than accepted and dropped. Per-job time is clamped to
`--max-seconds`. This is what keeps the solver endpoint from being an open
compute service.

**Dataset protection.** Uploaded instances live under `--root`, owned by the
service user, and are reachable only through the API with a session whose role
and project ownership both permit it.

**Backup and restore — procedure, untested.** `--root` holds
`trackaccess.sqlite` plus the instance and job directories. It contains no
credentials; passwords are PBKDF2 hashes and sessions are stored as hashes.

```bash
sudo systemctl stop trackaccess
sudo sqlite3 /var/lib/trackaccess/trackaccess.sqlite ".backup '/tmp/ta-backup.sqlite'"
sudo tar czf trackaccess-$(date +%F).tar.gz -C /var/lib trackaccess
sudo systemctl start trackaccess
```

Restore: stop, replace the directory, start. Instances are immutable once
written and plan versions are self-contained directories, so a restored store
needs no migration.

**No restore has been rehearsed, and no RTO or RPO is measured.** Proposed
targets, offered as targets only:

| Failure | Proposed RTO | Proposed RPO |
| --- | --- | --- |
| Worker process dies | none — the service stays up, one job fails | none |
| Service process dies | seconds, via `Restart=on-failure` | uncommitted in-flight jobs only |
| Host loss | bounded by rebuild + restore time | since the last backup |

Nothing here supports a zero-data-loss claim; that would need replication this
project does not implement.

## What is deliberately absent

- No multi-organisation isolation. Single tenant by design, not by omission.
- No telemetry, no outbound calls, no CDN. The interface loads only its own
  assets, so a standalone deployment works with no network at all.
- No password reset by email. An administrator sets a password directly.
