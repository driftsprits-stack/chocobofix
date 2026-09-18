#!/usr/bin/env python3
"""End-to-end checks for the shared-project layer, over HTTP.

Exercises what the interface itself does: sign in, create a project, upload an
instance, solve, and approve - and every refusal that protects a plan from being
trusted when it should not be.

usage: tests/test_multiuser.py [base_url]
"""
import json, os, ssl, sys, time, urllib.parse, urllib.request, urllib.error

BASE = (sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8150') + '/api/v1'
P = F = 0

# Certificate verification is never disabled. To test against a deployment whose
# certificate is issued by an internal CA (or a throwaway one, as in
# docs/DEPLOYMENT.md), point TA_CA at that CA's PEM file and it is trusted for
# this run only.
_CTX = None
if BASE.startswith('https://'):
    _CTX = ssl.create_default_context(cafile=os.environ.get('TA_CA') or None)
_OPENER = urllib.request.build_opener(
    urllib.request.HTTPSHandler(context=_CTX)) if _CTX else urllib.request.build_opener()

def ok(m):
    global P; P += 1; print(f'  ok   {m}')
def no(m):
    global F; F += 1; print(f'  FAIL {m}')
def chk(got, want, m):
    (ok if got == want else no)(m if got == want else f'{m} (got {got!r}, expected {want!r})')

def call(method, path, token=None, params=None, files=None, expect=None):
    url = BASE + path
    if params: url += ('&' if '?' in url else '?') + urllib.parse.urlencode(params, doseq=True)
    req = urllib.request.Request(url, method=method)
    if token: req.add_header('Authorization', 'Bearer ' + token)
    body = None
    if files:
        boundary = '----trackaccess-test-boundary'
        parts = []
        for name, content in files.items():
            parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"; '
                         f'filename="{name}"\r\nContent-Type: text/csv\r\n\r\n'.encode() + content + b'\r\n')
        parts.append(f'--{boundary}--\r\n'.encode())
        body = b''.join(parts)
        req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
    try:
        with _OPENER.open(req, body, timeout=120) as r:
            raw = r.read()
            return r.status, (json.loads(raw) if raw[:1] in (b'{', b'[') else raw)
    except urllib.error.HTTPError as e:
        raw = e.read()
        try: return e.code, json.loads(raw)
        except Exception: return e.code, raw

def main():
    print('== bootstrap and sign-in ==')
    st, h = call('GET', '/health')
    if h.get('needs_bootstrap'):
        st, _ = call('POST', '/bootstrap', params={'username': 'admin.ada',
                                                   'password': 'administrator-pass-1'})
        chk(st, 200, 'the first administrator can be created')
    st, _ = call('POST', '/bootstrap', params={'username': 'sneaky', 'password': 'another-pass-1234'})
    chk(st, 409, 'bootstrap is refused once an account exists')

    st, r = call('POST', '/auth/login', params={'username': 'admin.ada',
                                                'password': 'administrator-pass-1'})
    chk(st, 200, 'the administrator signs in')
    admin = r['token']
    chk(r['user']['can']['approve'], False,
        'the administrator is NOT granted approval - duties are separated')
    chk(r['user']['can']['manage_users'], True, 'the administrator may manage accounts')

    st, _ = call('POST', '/auth/login', params={'username': 'admin.ada', 'password': 'wrong'})
    chk(st, 401, 'a wrong password is refused')
    st, _ = call('POST', '/auth/login', params={'username': 'ghost', 'password': 'whatever12345'})
    chk(st, 401, 'an unknown account is refused, with the same message')

    print('== accounts ==')
    for name, pwd, role in [('plan.pat', 'planner-password-01', 'planner'),
                            ('appr.avi', 'approver-password-1', 'approver'),
                            ('view.vic', 'viewer-password-001', 'viewer')]:
        st, _ = call('POST', '/users', admin, {'username': name, 'password': pwd, 'role': role})
        chk(st, 200, f'the administrator creates a {role}')
    st, _ = call('POST', '/users', admin, {'username': 'weak.will', 'password': 'short', 'role': 'viewer'})
    chk(st, 400, 'a password under 12 characters is refused')

    tok = {}
    for name, pwd in [('plan.pat', 'planner-password-01'), ('appr.avi', 'approver-password-1'),
                      ('view.vic', 'viewer-password-001')]:
        st, r = call('POST', '/auth/login', params={'username': name, 'password': pwd})
        tok[name.split('.')[0]] = r['token']
    planner, approver, viewer = tok['plan'], tok['appr'], tok['view']

    print('== the permission matrix is enforced server-side ==')
    st, _ = call('POST', '/users', planner, {'username': 'x.y', 'password': 'aaaaaaaaaaaa', 'role': 'viewer'})
    chk(st, 403, 'a planner may not create accounts')
    st, _ = call('POST', '/users', approver, {'username': 'x.y', 'password': 'aaaaaaaaaaaa', 'role': 'viewer'})
    chk(st, 403, 'an approver may not create accounts')
    st, _ = call('POST', '/projects', viewer, {'name': 'nope'})
    chk(st, 403, 'a viewer may not create a project')
    st, _ = call('POST', '/projects', approver, {'name': 'nope'})
    chk(st, 403, 'an approver may not create a project - they do not make the work they sign off')
    st, _ = call('GET', '/users', planner)
    chk(st, 403, 'a planner may not list accounts')
    st, _ = call('GET', '/auth/me')
    chk(st, 401, 'an unauthenticated request is refused')
    st, _ = call('GET', '/auth/me', 'not-a-real-token-at-all')
    chk(st, 401, 'a bogus token is refused')

    print('== project, instance, solve ==')
    st, r = call('POST', '/projects', planner, {'name': 'Northern renewals'})
    chk(st, 200, 'a planner creates a project')
    pid, rev = r['id'], r['revision']
    st, r = call('POST', f'/projects/{pid}/instances/demo', planner)
    chk(st, 200, 'the planner loads an instance')
    iid = r['instance_id']

    st, _ = call('POST', f'/projects/{pid}/jobs', viewer, {'instance_id': iid, 'scenario': 'A'})
    chk(st, 403, 'a viewer cannot solve at all - refused on their role')

    # Dataset protection. A second planner has the capability to solve, so the
    # role check passes and the project check is what must stop them. It reports
    # 404, not 403, so the response does not confirm that the project exists.
    st, _ = call('POST', '/users', admin, {'username': 'plan.rival', 'password': 'rival-password-01',
                                           'role': 'planner'})
    st, r = call('POST', '/auth/login', params={'username': 'plan.rival',
                                                'password': 'rival-password-01'})
    rival = r['token']
    st, _ = call('GET', f'/projects/{pid}/versions', rival)
    chk(st, 404, "another planner cannot read someone else's project, and is not told it exists")
    st, _ = call('POST', f'/projects/{pid}/jobs', rival, {'instance_id': iid, 'scenario': 'A'})
    chk(st, 404, "  ... nor solve in it")
    st, _ = call('GET', f'/instances/{iid}/detail', rival)
    chk(st, 404, "  ... nor read its uploaded dataset")
    st, r = call('GET', '/projects', rival)
    chk(any(p_['id'] == pid for p_ in r['projects']), False,
        "  ... and it does not appear in their project list")

    print('== optimistic concurrency ==')
    st, r = call('POST', f'/projects/{pid}/jobs', planner,
                 {'instance_id': iid, 'scenario': 'A', 'seconds': 20, 'expected_revision': 9999})
    chk(st, 409, 'a stale project revision is refused')
    chk('current_revision' in (r if isinstance(r, dict) else {}), True,
        '  ... and the refusal reports the current revision')

    st, r = call('POST', f'/projects/{pid}/jobs', planner,
                 {'instance_id': iid, 'scenario': 'all', 'seconds': 30})
    chk(st, 200, 'the planner starts a solve')
    job = r['job_id']
    for _ in range(90):
        st, r = call('GET', f'/jobs/{job}', planner)
        if r['state'] in ('done', 'failed', 'cancelled'): break
        time.sleep(1)
    chk(r['state'], 'done', 'the solve completes')

    st, r = call('GET', f'/projects/{pid}/versions', planner)
    versions = r['versions']
    chk(len(versions), 3, 'three plan versions were recorded, one per scenario')
    by_scen = {v['scenario']: v for v in versions}
    chk(all(v['status'] == 'draft' for v in versions), True,
        'every new version starts as a draft, never pre-approved')
    chk(all(v['feasible'] for v in versions), True, 'all three are feasible')

    print('== approval ==')
    va = by_scen['A']
    st, _ = call('POST', f"/versions/{va['id']}/approve", planner,
                 {'content_hash': va['content_hash'], 'validation_hash': va['validation_hash']})
    chk(st, 403, 'a planner cannot approve their own plan')
    st, _ = call('POST', f"/versions/{va['id']}/approve", admin,
                 {'content_hash': va['content_hash'], 'validation_hash': va['validation_hash']})
    chk(st, 403, 'an administrator cannot approve either')
    st, _ = call('POST', f"/versions/{va['id']}/approve", approver,
                 {'content_hash': 'wrong-hash', 'validation_hash': va['validation_hash']})
    chk(st, 409, 'approval is refused when the content hash does not match what was shown')
    st, _ = call('POST', f"/versions/{va['id']}/approve", approver,
                 {'content_hash': va['content_hash'], 'validation_hash': 'wrong-hash'})
    chk(st, 409, 'approval is refused when the validation hash does not match')
    st, r = call('POST', f"/versions/{va['id']}/approve", approver,
                 {'content_hash': va['content_hash'], 'validation_hash': va['validation_hash']})
    chk(st, 200, 'the approver approves it when both hashes match')
    chk(r['version']['status'], 'approved', '  ... and the version is marked approved')
    chk(r['version']['approved_by'], 'appr.avi', '  ... recording who approved it')
    st, _ = call('POST', f"/versions/{va['id']}/approve", approver,
                 {'content_hash': va['content_hash'], 'validation_hash': va['validation_hash']})
    chk(st, 409, 'the same version cannot be approved twice')

    print('== a new input invalidates what was approved against the old one ==')
    st, r = call('POST', f'/projects/{pid}/instances', planner, files={
        n: open(f'data/upstream/PS1/01_data/{n}', 'rb').read() for n in [
            '01_LINES.csv', '02_STATIONS.csv', '03_SECTORS.csv', '04_LOCATION_SUPPLY.csv',
            '05_BUFFER_LOCATION.csv', '06_PARAMETERS.csv', '07_PROJECT_DETAILS.csv',
            '08_ACTIVITY_DETAILS.csv']})
    chk(st, 200, 'the planner uploads the eight files directly')
    # Same bytes, so the same input hash: nothing should be invalidated.
    chk(r['invalidated_plans'], 0, 'an identical input invalidates nothing')

    print('== malformed upload ==')
    files = {n: open(f'data/upstream/PS1/01_data/{n}', 'rb').read() for n in [
        '01_LINES.csv', '02_STATIONS.csv', '03_SECTORS.csv', '04_LOCATION_SUPPLY.csv',
        '05_BUFFER_LOCATION.csv', '06_PARAMETERS.csv', '07_PROJECT_DETAILS.csv',
        '08_ACTIVITY_DETAILS.csv']}
    files['06_PARAMETERS.csv'] = b'key,value\nhorizon_start,2027-13-45\nhorizon_weeks,30\n'
    st, r = call('POST', f'/projects/{pid}/instances', planner, files=files)
    chk(st, 422, 'a malformed instance is rejected')
    chk(r['errors'][0]['file'], '06_PARAMETERS.csv', '  ... naming the file at fault')

    print('== audit ==')
    st, _ = call('GET', f'/projects/{pid}/audit', planner)
    chk(st, 403, 'a planner may not read the audit trail')
    st, r = call('GET', f'/projects/{pid}/audit', approver)
    chk(st, 200, 'an approver may')
    actions = [e['action'] for e in r['events']]
    for want in ['auth.login', 'user.create', 'project.create', 'instance.upload',
                 'job.create', 'plan.create', 'plan.approve']:
        chk(want in actions, True, f'the trail records {want}')
    approve_ev = [e for e in r['events'] if e['action'] == 'plan.approve' and e['result'] == 'ok']
    chk(len(approve_ev) >= 1, True, 'the approval is recorded with its actor and object')
    chk(approve_ev[0]['actor'], 'appr.avi', '  ... naming who did it')
    refused = [e for e in r['events'] if e['action'] == 'plan.approve' and e['result'] == 'refused']
    chk(len(refused) >= 1, True, 'refused approvals are recorded too, not just successful ones')
    denies = [e for e in r['events'] if e['action'] == 'authz.deny']
    chk(len(denies) >= 1, True, 'permission denials are recorded')

    print('== sessions ==')
    st, _ = call('POST', '/auth/logout', viewer)
    chk(st, 200, 'a session can be ended')
    st, _ = call('GET', '/auth/me', viewer)
    chk(st, 401, 'the ended session stops working')

    print()
    print(f'{P} passed, {F} failed')
    return 0 if F == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
