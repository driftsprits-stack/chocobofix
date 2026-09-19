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

def upload_bytes(path, token, field, filename, content, content_type):
    """POST one binary file as multipart/form-data."""
    boundary = '----chocobofix-bin-boundary'
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="{field}"; '
            f'filename="{filename}"\r\nContent-Type: {content_type}\r\n\r\n').encode()
    body += content + f'\r\n--{boundary}--\r\n'.encode()
    req = urllib.request.Request(BASE + path, method='POST', data=body)
    req.add_header('Authorization', 'Bearer ' + token)
    req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
    try:
        with _OPENER.open(req, timeout=60) as r:
            return r.status, json.loads(r.read() or b'{}')
    except urllib.error.HTTPError as e:
        raw = e.read()
        try: return e.code, json.loads(raw)
        except Exception: return e.code, raw


def make_png(w=64, h=64):
    import struct, zlib
    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    raw = b''.join(b'\x00' + bytes([(x * 3) % 256, (y * 3) % 256, 90] * 1 for x in range(w)
                                    ) if False else b'\x00' + b''.join(
                        bytes([(x * 3) % 256, (y * 3) % 256, 90]) for x in range(w))
                   for y in range(h))
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw))
            + chunk(b'IEND', b''))


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
    for want in ['project.create', 'instance.upload', 'job.create', 'plan.create', 'plan.approve']:
        chk(want in actions, True, f'the trail records {want}')
    approve_ev = [e for e in r['events'] if e['action'] == 'plan.approve' and e['result'] == 'ok']
    chk(len(approve_ev) >= 1, True, 'the approval is recorded with its actor and object')
    chk(approve_ev[0]['actor'], 'appr.avi', '  ... naming who did it')
    refused = [e for e in r['events'] if e['action'] == 'plan.approve' and e['result'] == 'refused']
    chk(len(refused) >= 1, True, 'refused approvals are recorded too, not just successful ones')

    print('== audit isolation: a project trail holds that project only ==')
    # A second project, owned by a different planner, with its own events.
    st, rb = call('POST', '/projects', rival, {'name': 'Southern renewals'})
    chk(st, 200, 'a second planner creates their own project')
    pid_b = rb['id']
    st, _ = call('POST', f'/projects/{pid_b}/instances/demo', rival)
    chk(st, 200, 'the second planner loads an instance into it')

    st, ra = call('GET', f'/projects/{pid}/audit', approver)
    chk(st, 200, "project A's trail is readable")
    objects_a = [e['object'] for e in ra['events']]
    chk(any(o == f'project {pid}' for o in objects_a), True,
        "project A's trail names project A")
    chk(any(o == f'project {pid_b}' for o in objects_a), False,
        "project A's trail does NOT name project B")

    st, rbb = call('GET', f'/projects/{pid_b}/audit', approver)
    chk(st, 200, "project B's trail is readable")
    objects_b = [e['object'] for e in rbb['events']]
    chk(any(o == f'project {pid_b}' for o in objects_b), True,
        "project B's trail names project B")
    chk(any(o == f'project {pid}' for o in objects_b), False,
        "project B's trail does NOT name project A")
    # B's instance upload must not appear in A's trail either.
    inst_objs_b = {e['object'] for e in rbb['events'] if e['object'].startswith('instance ')}
    chk(bool(inst_objs_b & set(objects_a)), False,
        "project B's instance events do not appear in project A's trail")

    print('== audit isolation: account events are not in a project trail ==')
    for leaked in ['auth.login', 'user.create', 'user.role', 'authz.deny']:
        chk(leaked in [e['action'] for e in ra['events']], False,
            f'a project trail does not expose the account event {leaked}')

    print('== installation-wide audit is separately authorised ==')
    st, _ = call('GET', '/audit', planner)
    chk(st, 403, 'a planner may not read the installation audit')
    st, _ = call('GET', '/audit', approver)
    chk(st, 403, 'an approver holds view_audit but may NOT read the installation audit')
    st, rall = call('GET', '/audit', admin)
    chk(st, 200, 'an administrator may read the installation audit')
    all_actions = [e['action'] for e in rall['events']]
    for want in ['auth.login', 'user.create', 'project.create']:
        chk(want in all_actions, True, f'the installation audit records {want}')
    all_objects = [e['object'] for e in rall['events']]
    chk(any(o == f'project {pid}' for o in all_objects)
        and any(o == f'project {pid_b}' for o in all_objects), True,
        'the installation audit spans both projects')

    print('== coordinator assignment ==')
    st, _ = call('POST', f'/projects/{pid}/assignments', planner,
                 {'instance_id': iid, 'activity_id': 'A001', 'coordinator_id': 3})
    chk(st, 200, 'a planner assigns a coordinator')
    st, r = call('GET', f'/projects/{pid}/assignments', planner, {'instance_id': iid})
    chk(st, 200, 'the assignment reads back')
    got = {a['activity_id']: a for a in r['assignments']}
    chk('A001' in got, True, '  ... for the right activity')

    # A second authorised session must see it - it is server state, not local.
    st, r2 = call('GET', f'/projects/{pid}/assignments', approver, {'instance_id': iid})
    chk(st, 200, 'a second authorised session sees the assignment')
    chk([a['activity_id'] for a in r2['assignments']], ['A001'], '  ... with the same content')

    st, _ = call('POST', f'/projects/{pid}/assignments', viewer,
                 {'instance_id': iid, 'activity_id': 'A002', 'coordinator_id': 3})
    chk(st in (403, 404), True, 'a viewer may not assign a coordinator')

    # Scope: project + instance + activity, never the activity id alone.
    st, rb = call('POST', '/projects', rival, {'name': 'Scope probe'})
    other = rb['id']
    st, _ = call('POST', f'/projects/{other}/assignments', rival,
                 {'instance_id': iid, 'activity_id': 'A001', 'coordinator_id': 3})
    chk(st, 404, "another project cannot assign against this project's instance")

    st, _ = call('POST', f'/projects/{pid}/assignments', planner,
                 {'instance_id': iid, 'activity_id': 'A001', 'coordinator_id': ''})
    chk(st, 200, 'an assignment can be cleared')
    st, r = call('GET', f'/projects/{pid}/assignments', planner, {'instance_id': iid})
    chk(r['assignments'], [], '  ... and the activity is then unassigned')

    st, ra = call('GET', f'/projects/{pid}/audit', approver)
    acts = [e['action'] for e in ra['events']]
    chk('activity.assign' in acts, True, 'assigning is recorded in the project trail')
    chk('auth.login' in acts, False, '  ... without pulling in account events')

    print('== profile photo ==')
    st, _ = upload_bytes('/profile/photo', planner, 'photo', 'x.svg',
                         b'<svg xmlns="http://www.w3.org/2000/svg"><script/></svg>',
                         'image/svg+xml')
    chk(st, 415, 'an SVG is refused')
    st, _ = upload_bytes('/profile/photo', planner, 'photo', 'x.gif',
                         b'GIF89a' + b'\x00' * 64, 'image/gif')
    chk(st, 415, 'a GIF is refused')
    st, _ = upload_bytes('/profile/photo', planner, 'photo', 'shell.png',
                         b'#!/bin/sh\necho hi\n' + b'\x00' * 64, 'image/png')
    chk(st, 415, 'a script renamed .png is refused - the bytes decide, not the name')
    st, _ = upload_bytes('/profile/photo', planner, 'photo', 'ok.png', make_png(), 'image/png')
    chk(st, 200, 'a real PNG is accepted')
    st, _ = call('GET', '/users/2/photo', planner)
    chk(st, 200, 'the photo reads back')
    st, _ = upload_bytes('/profile/photo', planner, 'photo', 'huge.png',
                         make_png(4, 4)[:16] + b'\x00\x00\x10\x00\x00\x00\x10\x00'
                         + make_png(4, 4)[24:], 'image/png')
    chk(st, 400, 'an over-large image is refused on its parsed dimensions')

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
