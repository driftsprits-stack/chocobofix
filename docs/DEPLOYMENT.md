# Deployment

**Nothing is deployed. No hosted URL exists for this project.** Everything below
is the procedure to create one; it has not been executed, because it needs
hosting and DNS credentials this project does not hold. Do not read any part of
this file as a record of something that happened.

## What the judging deliverable needs

PS1 §4.2 requires a reachable web application where a judge can upload the eight
instance CSVs, run the scheduler, see the result and validate it. The application
does that today on localhost (`tests/integration.sh` exercises the whole path).
What remains is putting it on a public address with TLS.

## Shape

```
  judge ──HTTPS──▶ reverse proxy (TLS) ──HTTP──▶ trackaccess-service ──▶ worker
                   caddy / nginx                 127.0.0.1:8080          processes
```

The service speaks plain HTTP and has no certificate handling of its own. That is
deliberate: TLS termination, renewal and rotation belong to a component built for
it. **Never expose the service directly on a public interface.**

## 1. Build on the target host

```bash
git clone <repo> trackaccess && cd trackaccess
./scripts/fetch_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/test_core && ./build/check_sample        # refuse to deploy if these fail
```

## 2. Run the service bound to loopback

```bash
./build/trackaccess-service \
  --host 127.0.0.1 --port 8080 --auth none \
  --root /var/lib/trackaccess --web ./web \
  --worker ./build/trackaccess \
  --public-instance ./data/upstream/PS1/01_data \
  --max-solves 2 --max-seconds 120
```

`--auth none` is correct **only** behind a proxy for open judging. For any
deployment holding real planning data, drop it and use the printed bearer token.

systemd unit:

```ini
[Unit]
Description=TrackAccess service
After=network.target

[Service]
User=trackaccess
WorkingDirectory=/opt/trackaccess
ExecStart=/opt/trackaccess/build/trackaccess-service --host 127.0.0.1 --port 8080 \
  --auth none --root /var/lib/trackaccess --web /opt/trackaccess/web \
  --worker /opt/trackaccess/build/trackaccess \
  --public-instance /opt/trackaccess/data/upstream/PS1/01_data
Restart=on-failure
RestartSec=2
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ReadWritePaths=/var/lib/trackaccess

[Install]
WantedBy=multi-user.target
```

## 3. Terminate TLS

Caddy obtains and renews a certificate automatically:

```
trackaccess.example.org {
    encode gzip
    request_body { max_size 32MB }
    reverse_proxy 127.0.0.1:8080
}
```

nginx equivalent: `proxy_pass http://127.0.0.1:8080;` with
`client_max_body_size 32m;` and certificates from certbot.

**Certificate rotation.** Caddy and certbot both renew automatically; verify with
`caddy list-certificates` or `certbot certificates` and alert on fewer than 21
days remaining. On an isolated company network with an internal CA, install the
CA certificate into the host trust store and issue from it. **Never disable
certificate verification to work around an expiry** — that converts a visible,
fixable problem into a silent one.

## 4. Verify the deployment

```bash
curl -s https://trackaccess.example.org/api/v1/health
PORT=443 ./tests/integration.sh build     # adjust the base URL first
```

A deployment is only finished when a judge can upload eight files and download
three, so walk that path in a browser before declaring it done.

## Backup and restore — procedure, untested

`--root` holds everything stateful: instance snapshots and job outputs. It
contains no credentials.

```bash
systemctl stop trackaccess          # or accept a crash-consistent copy
tar czf trackaccess-$(date +%F).tar.gz -C /var/lib trackaccess
```

Restore: stop the service, replace the directory, start it. Instances are
immutable once written and jobs are self-contained directories, so a restored
store needs no migration.

**No restore has been rehearsed, and no RTO or RPO has been measured.** Proposed
targets, offered as targets only:

| Failure | Proposed RTO | Proposed RPO |
| --- | --- | --- |
| Worker process dies | none — the service stays up, one job fails | none |
| Service process dies | seconds, via `Restart=on-failure` | uncommitted in-flight jobs only |
| Host loss | bounded by rebuild + restore time | since the last backup |

These are proposals, not measurements. Nothing here supports a zero-data-loss
claim; achieving one would need replication this project does not implement.

## What is deliberately absent

- No user accounts, roles or approvals — the service has one shared token.
- No database. The store is files.
- No multi-organisation isolation. Single tenant by design, not by omission.
- No telemetry, no outbound calls, no CDN. The interface loads only its own
  assets, so the standalone deployment works with no network at all.
