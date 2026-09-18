'use strict';
/* TrackAccess browser client.
 *
 * Renders from the competition CSV files the service actually produced, not from
 * a parallel in-memory copy: what the planner sees on screen is what downloads.
 */

// ---------------------------------------------------------------- utilities
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
const el = (t, a = {}, ...kids) => {
  const n = document.createElement(t);
  for (const [k, v] of Object.entries(a)) {
    if (k === 'class') n.className = v;
    else if (k === 'text') n.textContent = v;
    else if (k.startsWith('on')) n.addEventListener(k.slice(2), v);
    else n.setAttribute(k, v);
  }
  for (const c of kids) if (c != null) n.append(c);
  return n;
};
const esc = s => String(s).replace(/[<>&"]/g, c => ({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]));

function parseCsv(text) {
  const rows = [];
  let row = [], cur = '', q = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (q) {
      if (c === '"') { if (text[i+1] === '"') { cur += '"'; i++; } else q = false; }
      else cur += c;
    } else if (c === '"') q = true;
    else if (c === ',') { row.push(cur); cur = ''; }
    else if (c === '\n') { row.push(cur); cur = ''; if (row.some(x => x !== '')) rows.push(row); row = []; }
    else if (c !== '\r') cur += c;
  }
  row.push(cur);
  if (row.some(x => x !== '')) rows.push(row);
  if (!rows.length) return [];
  const head = rows[0].map(h => h.trim());
  return rows.slice(1).map(r => Object.fromEntries(head.map((h, i) => [h, (r[i] ?? '').trim()])));
}

const FILES = ['01_LINES.csv','02_STATIONS.csv','03_SECTORS.csv','04_LOCATION_SUPPLY.csv',
               '05_BUFFER_LOCATION.csv','06_PARAMETERS.csv','07_PROJECT_DETAILS.csv',
               '08_ACTIVITY_DETAILS.csv'];

// ---------------------------------------------------------------- state
const S = {
  lang: localStorage.getItem('ta.lang') || 'en',
  token: sessionStorage.getItem('ta.token') || '',
  user: null,
  projectId: null, projectRev: 0, projectName: '',
  instanceId: null, detail: null, jobId: null, poll: null,
  picked: new Map(), results: {}, scenarios: [], current: null, selectedActivity: null,
  versions: [], versionByScenario: {}
};

const T = k => (window.I18N[S.lang] && window.I18N[S.lang][k]) || window.I18N.en[k] || k;

async function api(path, opts = {}) {
  const h = Object.assign({}, opts.headers || {});
  if (S.token) h['Authorization'] = 'Bearer ' + S.token;
  // A correlation id travels with every request and lands in the audit trail,
  // so an operator can tie what they did to what the server recorded.
  h['X-Correlation-Id'] = 'ui-' + Math.random().toString(36).slice(2, 10);
  const r = await fetch(path, Object.assign({}, opts, { headers: h }));
  const ct = r.headers.get('content-type') || '';
  const body = ct.includes('json') ? await r.json() : await r.text();
  if (r.status === 401 && S.token) {
    // The session expired or was revoked. Return to the gate rather than
    // leaving controls on screen that will keep failing.
    S.token = ''; S.user = null; sessionStorage.removeItem('ta.token');
    showGate(T('session_expired'));
  }
  if (!r.ok) throw Object.assign(new Error((body && body.error) || r.statusText), { status: r.status, body });
  return body;
}

// ---------------------------------------------------------------- i18n + chrome
function applyLang() {
  document.documentElement.lang = S.lang;
  $$('[data-i18n]').forEach(n => { const v = T(n.dataset.i18n); if (v) n.textContent = v; });
  $('#langSel').value = S.lang;
  updateScenarioHelp();
  if (S.current) renderAll();
}
$('#langSel').addEventListener('change', e => {
  S.lang = e.target.value; localStorage.setItem('ta.lang', S.lang); applyLang();
});
$('#sizeSel').addEventListener('change', e => {
  document.documentElement.style.setProperty('--fs', e.target.value + 'px');
});

function showTab(name) {
  $$('#tabs button').forEach(b => b.setAttribute('aria-selected', String(b.dataset.tab === name)));
  ['projects','import','solve','schedule','network','check','repair','versions','export','audit','users']
    .forEach(t => $('#p-' + t).classList.toggle('hide', t !== name));
  if (name === 'network') drawNetwork();
  if (name === 'repair') fillRepairLocations();
  if (name === 'versions') loadVersions();
  if (name === 'audit') loadAudit();
  if (name === 'users') loadUsers();
}
$$('#tabs button').forEach(b => b.addEventListener('click', () => { if (!b.disabled) showTab(b.dataset.tab); }));
const enableTabs = on => ['solve','schedule','network','check','repair','export']
  .forEach(t => { $(`#tabs button[data-tab="${t}"]`).disabled = !on; });

// ---------------------------------------------------------------- auth
let GATE_MODE = 'login';   // or 'bootstrap'

function showGate(msg) {
  $('#gate').classList.remove('hide');
  $('#app').classList.add('hide');
  $('#tabs').classList.add('hide');
  $('#gateMsg').innerHTML = msg ? `<span class="chip bad">${esc(msg)}</span>` : '';
}
function hideGate() {
  $('#gate').classList.add('hide');
  $('#app').classList.remove('hide');
  $('#tabs').classList.remove('hide');
}

function renderUserChip() {
  if (!S.user) { $('#userChip').textContent = ''; return; }
  const c = $('#userChip');
  c.textContent = '';
  c.append(el('b', { text: S.user.username }),
           document.createTextNode(' · '),
           el('span', { text: S.user.role }),
           document.createTextNode(' '),
           el('button', { class: 'btn', text: T('signout'), style: 'padding:0 8px', onclick: async () => {
             try { await api('/api/v1/auth/logout', { method: 'POST' }); } catch (e) {}
             S.token = ''; S.user = null; sessionStorage.removeItem('ta.token');
             showGate('');
           } }));
  // Role-gated tabs: hidden entirely rather than shown and refused.
  $('#tabs button[data-tab="audit"]').classList.toggle('hide', !S.user.can.view_audit);
  $('#tabs button[data-tab="users"]').classList.toggle('hide', !S.user.can.manage_users);
  $('#prjNewRow').classList.toggle('hide', !S.user.can.create_project);
}

$('#gateGo').addEventListener('click', doGate);
$('#gp').addEventListener('keydown', e => { if (e.key === 'Enter') doGate(); });
$('#gu').addEventListener('keydown', e => { if (e.key === 'Enter') doGate(); });

async function doGate() {
  const u = $('#gu').value.trim(), p = $('#gp').value;
  if (!u || !p) { $('#gateMsg').innerHTML = `<span class="chip bad">${esc(T('need_both'))}</span>`; return; }
  const path = GATE_MODE === 'bootstrap' ? '/api/v1/bootstrap' : '/api/v1/auth/login';
  const q = new URLSearchParams({ username: u, password: p });
  try {
    const r = await api(path + '?' + q.toString(), { method: 'POST' });
    if (GATE_MODE === 'bootstrap') { GATE_MODE = 'login'; await doGate(); return; }
    S.token = r.token; S.user = r.user;
    sessionStorage.setItem('ta.token', S.token);
    $('#gp').value = '';
    hideGate(); renderUserChip(); await loadProjects();
    showTab('projects');
  } catch (e) {
    $('#gateMsg').innerHTML = `<span class="chip bad">${esc(e.message)}</span>`;
  }
}

async function boot() {
  const h = await api('/api/v1/health');
  if (h.needs_bootstrap) {
    GATE_MODE = 'bootstrap';
    $('#gateTitle').textContent = T('first_admin');
    $('#gateHelp').textContent = T('first_admin_help');
    $('#gateGo').textContent = T('create_admin');
    showGate('');
    return;
  }
  if (S.token) {
    try {
      const me = await api('/api/v1/auth/me');
      S.user = me.user;
      hideGate(); renderUserChip(); await loadProjects();
      return;
    } catch (e) { /* fall through to the gate */ }
  }
  showGate('');
}

// ---------------------------------------------------------------- 1 projects
async function loadProjects() {
  const r = await api('/api/v1/projects');
  const tb = $('#prjTable tbody'); tb.textContent = '';
  r.projects.forEach(p => {
    const tr = el('tr', { tabindex: '0' },
      el('td', { text: p.name }), el('td', { text: p.owner }),
      el('td', { class: 'num', text: p.revision }), el('td', { text: p.created_at.slice(0, 16) }));
    const pick = () => selectProject(p);
    tr.addEventListener('click', pick);
    tr.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); pick(); } });
    if (p.id === S.projectId) tr.classList.add('sel');
    tb.append(tr);
  });
  if (!r.projects.length)
    tb.append(el('tr', {}, el('td', { colspan: '4', class: 'note',
      text: S.user && S.user.can.create_project ? T('no_projects_planner') : T('no_projects') })));
}

async function selectProject(p) {
  S.projectId = p.id; S.projectRev = p.revision; S.projectName = p.name;
  S.instanceId = null; S.detail = null; S.results = {}; S.scenarios = []; S.current = null;
  $$('#prjTable tbody tr').forEach(x => x.classList.remove('sel'));
  enableTabs(false);
  $('#tabs button[data-tab="import"]').disabled = !(S.user && S.user.can.run_solve);
  await loadInstances();
  await loadVersions();
  $('#tabs button[data-tab="versions"]').disabled = false;
  await loadProjects();
}

async function loadInstances() {
  if (!S.projectId) return;
  const r = await api(`/api/v1/projects/${S.projectId}/instances`);
  const tb = $('#instTable tbody'); tb.textContent = '';
  r.instances.forEach(i => {
    const tr = el('tr', { tabindex: '0' },
      el('td', {}, el('b', { text: '#' + i.id }), el('div', { class: 'note', text: i.label || '' })),
      el('td', { text: i.input_hash.slice(0, 16) + '…' }),
      el('td', { text: i.uploaded_by }), el('td', { text: i.created_at.slice(0, 16) }));
    const pick = async () => {
      $$('#instTable tbody tr').forEach(x => x.classList.remove('sel'));
      tr.classList.add('sel');
      await useInstance(i.id);
    };
    tr.addEventListener('click', pick);
    tr.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); pick(); } });
    tb.append(tr);
  });
  if (!r.instances.length)
    tb.append(el('tr', {}, el('td', { colspan: '4', class: 'note', text: T('no_instances') })));
}

$('#prjCreate').addEventListener('click', async () => {
  const name = $('#prjName').value.trim();
  if (!name) return;
  try {
    const p = await api('/api/v1/projects?' + new URLSearchParams({ name }).toString(), { method: 'POST' });
    $('#prjName').value = '';
    $('#prjMsg').innerHTML = '<span class="chip ok">created</span>';
    await loadProjects();
    await selectProject({ id: p.id, revision: p.revision, name: p.name });
  } catch (e) { $('#prjMsg').innerHTML = `<span class="chip bad">${esc(e.message)}</span>`; }
});

// ---------------------------------------------------------------- 2 import
function renderPicked() {
  const fl = $('#fl'); fl.textContent = '';
  FILES.forEach(f => {
    const got = S.picked.has(f);
    fl.append(el('div', {},
      el('span', { text: f }),
      el('span', { class: got ? 'got' : 'miss', text: got ? '✓ ' + S.picked.get(f).size + ' B' : '—' })));
  });
  $('#upload').disabled = S.picked.size !== 8;
}
function addFiles(list) {
  for (const f of list) if (FILES.includes(f.name)) S.picked.set(f.name, f);
  renderPicked();
}
$('#pick').addEventListener('click', () => $('#files').click());
$('#files').addEventListener('change', e => addFiles(e.target.files));
const drop = $('#drop');
['dragenter','dragover'].forEach(ev => drop.addEventListener(ev, e => {
  e.preventDefault(); drop.classList.add('over');
}));
['dragleave','drop'].forEach(ev => drop.addEventListener(ev, e => {
  e.preventDefault(); drop.classList.remove('over');
}));
drop.addEventListener('drop', e => addFiles(e.dataTransfer.files));
drop.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $('#files').click(); } });

function showImportErrors(errs) {
  $('#impIdle').classList.add('hide');
  $('#impOk').classList.add('hide');
  const t = $('#impErrTable'); t.classList.remove('hide');
  const tb = $('tbody', t); tb.textContent = '';
  errs.slice(0, 300).forEach(e => tb.append(el('tr', { class: 'err' },
    el('td', { text: e.file }), el('td', { class: 'num', text: e.row || '' }),
    el('td', { text: e.field || '' }), el('td', { text: e.message, style: 'white-space:normal' }))));
}
async function useInstance(id) {
  S.instanceId = id;
  S.detail = await api(`/api/v1/instances/${id}/detail`);
  const s = S.detail.summary;
  $('#instMeta').innerHTML = `<b>${esc(s.activities)}</b> activities · <b>${esc(s.contracts)}</b> contracts · ${esc(s.horizon_weeks)} wk · <code>${esc(s.input_hash.slice(0,8))}</code>`;
  $('#wk').max = s.horizon_weeks;
  enableTabs(true);
  showTab('solve');
}

async function afterLoad(res) {
  await loadInstances();
  await useInstance(res.instance_id);
  const s = res.summary;
  $('#impErrTable').classList.add('hide');
  $('#impIdle').classList.add('hide');
  $('#impOk').classList.remove('hide');
  const dl = $('#impSummary'); dl.textContent = '';
  const add = (k, v) => { dl.append(el('dt', { text: k }), el('dd', { text: v })); };
  add('Activities', s.activities); add('Contracts', s.contracts);
  add('Locations', s.locations);
  add('Horizon', `${s.horizon_weeks} weeks from ${s.horizon_start}`);
  add('Total access-nights required', s.total_accesses);
  add('Buffer-conflicting pairs', s.exclusive_pairs);
  add('Input hash', s.input_hash.slice(0, 24) + '…');
  if (res.invalidated_plans)
    $('#impStatus').innerHTML =
      `<span class="chip ok">accepted</span> <span class="chip warn">${res.invalidated_plans} earlier plan(s) invalidated</span>`;
  else $('#impStatus').innerHTML = `<span class="chip ok">accepted</span>`;
}
$('#upload').addEventListener('click', async () => {
  $('#impStatus').innerHTML = '<span class="chip idle">checking…</span>';
  const fd = new FormData();
  for (const [name, file] of S.picked) fd.append(name, file, name);
  try {
    await afterLoad(await api(`/api/v1/projects/${S.projectId}/instances`, { method: 'POST', body: fd }));
  } catch (e) {
    $('#impStatus').innerHTML = '<span class="chip bad">rejected</span>';
    if (e.body && e.body.errors) showImportErrors(e.body.errors);
    else showImportErrors([{ file: '', row: 0, field: '', message: e.message }]);
  }
});
$('#demo').addEventListener('click', async () => {
  $('#impStatus').innerHTML = '<span class="chip idle">loading…</span>';
  try { await afterLoad(await api(`/api/v1/projects/${S.projectId}/instances/demo`, { method: 'POST' })); }
  catch (e) {
    $('#impStatus').innerHTML = '<span class="chip bad">unavailable</span>';
    showImportErrors([{ file: '', row: 0, field: '', message: e.message }]);
  }
});

// ---------------------------------------------------------------- 2 solve
function updateScenarioHelp() {
  const v = $('#scen').value;
  $('#scenHelp').textContent = T({ A: 's_a', B: 's_b', C: 's_c', all: 's_all' }[v]);
}
$('#scen').addEventListener('change', updateScenarioHelp);

$('#run').addEventListener('click', async () => {
  $('#log').textContent = '';
  S.results = {}; S.scenarios = []; S.current = null;
  const p = new URLSearchParams({
    instance_id: S.instanceId, scenario: $('#scen').value, seconds: $('#secs').value,
    expected_revision: S.projectRev
  });
  try {
    const job = await api(`/api/v1/projects/${S.projectId}/jobs?` + p.toString(), { method: 'POST' });
    S.jobId = job.job_id;
    $('#run').disabled = true; $('#cancel').disabled = false;
    $('#jobChip').innerHTML = '<span class="chip idle">queued</span>';
    S.poll = setInterval(pollJob, 700);
  } catch (e) {
    if (e.status === 409 && e.body) {
      // Someone else changed the project while this planner was working.
      $('#jobChip').innerHTML =
        `<span class="chip warn">${esc(e.message)} — you had revision ${esc(e.body.your_revision)}, ` +
        `it is now ${esc(e.body.current_revision)}</span> `;
      $('#jobChip').append(el('button', { class: 'btn', text: T('reload'), onclick: async () => {
        S.projectRev = e.body.current_revision;
        await loadVersions();
        $('#jobChip').innerHTML = `<span class="chip idle">${esc(T('reloaded'))}</span>`;
      } }));
    } else {
      $('#jobChip').innerHTML = `<span class="chip bad">${esc(e.message)}</span>`;
    }
  }
});
$('#cancel').addEventListener('click', async () => {
  try { await api(`/api/v1/jobs/${S.jobId}/cancel`, { method: 'POST' }); } catch (e) {}
});

async function pollJob() {
  let j;
  try { j = await api(`/api/v1/jobs/${S.jobId}`); } catch (e) { return; }
  const cls = { done: 'ok', failed: 'bad', cancelled: 'warn', running: 'idle', queued: 'idle' }[j.state];
  $('#jobChip').innerHTML = `<span class="chip ${cls}">${esc(j.state)}</span>` +
    (j.error ? ` <span class="note">${esc(j.error)}</span>` : '');
  try {
    const log = await api(`/api/v1/jobs/${S.jobId}/log`);
    $('#log').textContent = log; $('#log').scrollTop = $('#log').scrollHeight;
  } catch (e) {}
  if (['done','failed','cancelled'].includes(j.state)) {
    clearInterval(S.poll); S.poll = null;
    $('#run').disabled = false; $('#cancel').disabled = true;
    await collectResults();
  }
}

async function collectResults() {
  // Read the recorded plan versions rather than the job directory: a version is
  // the immutable thing an approver signs off, so the screen shows exactly what
  // can be approved.
  await loadVersions();
  const job = await api(`/api/v1/jobs/${S.jobId}`).catch(() => null);
  const mine = job ? new Set(job.version_ids) : null;
  S.results = {}; S.scenarios = [];
  const candidates = S.versions.filter(v => !mine || mine.has(v.id));
  for (const v of candidates) {
    try {
      const [acc, occ, rs, val] = await Promise.all([
        api(`/api/v1/versions/${v.id}/files/SCHEDULE_ACCESS.csv`),
        api(`/api/v1/versions/${v.id}/files/SCHEDULE_OCCUPANCY.csv`),
        api(`/api/v1/versions/${v.id}/files/RESULTS.csv`),
        api(`/api/v1/versions/${v.id}/validation`)
      ]);
      S.results[v.scenario] = {
        access: parseCsv(acc), occupancy: parseCsv(occ), results: parseCsv(rs),
        validation: val, version: v,
        raw: { SCHEDULE_ACCESS: acc, SCHEDULE_OCCUPANCY: occ, RESULTS: rs }
      };
      if (!S.scenarios.includes(v.scenario)) S.scenarios.push(v.scenario);
    } catch (e) { /* not produced */ }
  }
  S.scenarios.sort();
  if (S.scenarios.length) { S.current = S.scenarios[0]; renderAll(); showTab('schedule'); }
}

// ---------------------------------------------------------------- 8 versions
async function loadVersions() {
  if (!S.projectId) return;
  const r = await api(`/api/v1/projects/${S.projectId}/versions`);
  S.versions = r.versions;
  S.projectRev = r.revision;
  const tb = $('#verTable tbody'); tb.textContent = '';
  r.versions.forEach(v => {
    const statusChip = el('span', {
      class: 'chip ' + (v.status === 'approved' ? 'ok' : v.status === 'invalidated' ? 'bad'
                        : v.status === 'superseded' ? 'idle' : 'idle'),
      text: v.status });
    const flags = el('span', {});
    if (!v.feasible) flags.append(el('span', { class: 'chip bad', text: `${v.violations} violations` }));
    if (v.is_fallback) flags.append(el('span', { class: 'chip warn', text: 'not submission-ready' }));
    if (v.strict_buffers) flags.append(el('span', { class: 'chip idle', text: 'strict buffers' }));

    const action = el('span', {});
    if (S.user && S.user.can.approve) {
      if (v.approvable) {
        action.append(el('button', { class: 'btn primary', text: T('approve'), onclick: async () => {
          try {
            await api(`/api/v1/versions/${v.id}/approve?` + new URLSearchParams({
              content_hash: v.content_hash, validation_hash: v.validation_hash }).toString(),
              { method: 'POST' });
            await loadVersions();
          } catch (e) { alert(e.message); }
        } }));
      } else if (v.status === 'approved') {
        action.append(el('button', { class: 'btn', text: T('revoke'), onclick: async () => {
          const reason = prompt(T('revoke_why') || 'Reason for revoking?');
          if (reason === null) return;
          try {
            await api(`/api/v1/versions/${v.id}/revoke?` + new URLSearchParams({ reason }).toString(),
                      { method: 'POST' });
            await loadVersions();
          } catch (e) { alert(e.message); }
        } }));
      } else {
        action.append(el('span', { class: 'note', text: v.not_approvable_because }));
      }
    } else {
      action.append(el('span', { class: 'note', text: v.not_approvable_because || '—' }));
    }
    tb.append(el('tr', {},
      el('td', {}, el('b', { text: 'v' + v.version_no }),
         el('div', { class: 'note', text: v.content_hash.slice(0, 12) })),
      el('td', { text: v.scenario }),
      el('td', {}, statusChip, flags),
      el('td', { class: 'num', text: v.objective }),
      el('td', { text: v.created_by }),
      el('td', { text: v.created_at.slice(0, 16) }),
      el('td', {}, action)));
  });
  if (!r.versions.length)
    tb.append(el('tr', {}, el('td', { colspan: '7', class: 'note', text: T('no_versions') })));
}

// ---------------------------------------------------------------- audit
async function loadAudit() {
  if (!S.projectId) return;
  const r = await api(`/api/v1/projects/${S.projectId}/audit`);
  const tb = $('#audTable tbody'); tb.textContent = '';
  r.events.forEach(e => tb.append(el('tr', {},
    el('td', { text: e.ts.replace('T', ' ').replace('Z', '') }),
    el('td', { text: e.actor || '—' }),
    el('td', { text: e.action }),
    el('td', { text: e.object }),
    el('td', {}, el('span', { class: 'chip ' + (e.result === 'ok' ? 'ok' : e.result === 'denied' || e.result === 'refused' ? 'bad' : 'idle'), text: e.result })),
    el('td', { text: e.detail, style: 'white-space:normal' }))));
}

// ---------------------------------------------------------------- accounts
async function loadUsers() {
  const r = await api('/api/v1/users');
  const tb = $('#usrTable tbody'); tb.textContent = '';
  r.users.forEach(u => {
    const act = el('span', {});
    if (u.id !== S.user.id)
      act.append(el('button', { class: 'btn', text: u.disabled ? T('enable') : T('disable'),
        onclick: async () => {
          await api(`/api/v1/users/${u.id}/disable?disabled=${u.disabled ? '0' : '1'}`, { method: 'POST' });
          await loadUsers();
        } }));
    tb.append(el('tr', {},
      el('td', { text: u.username }),
      el('td', { text: u.role }),
      el('td', {}, el('span', { class: 'chip ' + (u.disabled ? 'bad' : 'ok'),
                                text: u.disabled ? 'disabled' : 'active' })),
      el('td', {}, act)));
  });
}
$('#usrCreate').addEventListener('click', async () => {
  const q = new URLSearchParams({ username: $('#nu').value.trim(), password: $('#np').value,
                                  role: $('#nr').value });
  try {
    await api('/api/v1/users?' + q.toString(), { method: 'POST' });
    $('#nu').value = ''; $('#np').value = '';
    $('#usrMsg').innerHTML = '<span class="chip ok">created</span>';
    await loadUsers();
  } catch (e) { $('#usrMsg').innerHTML = `<span class="chip bad">${esc(e.message)}</span>`; }
});

// ---------------------------------------------------------------- rendering
function scenarioSwitcher() {
  return el('div', { class: 'row', style: 'margin-bottom:8px' },
    el('label', { text: 'Scenario' }),
    ...S.scenarios.map(sc => el('button', {
      class: 'btn' + (sc === S.current ? ' primary' : ''), text: sc,
      onclick: () => { S.current = sc; renderAll(); }
    })));
}

function renderAll() { renderScore(); renderContracts(); renderGantt(); renderActivities(); renderCheck(); renderExport(); drawNetwork(); }

function renderScore() {
  const box = $('#scoreCards'); box.textContent = '';
  const host = box.parentElement;
  let sw = $('#scenSwitch');
  if (sw) sw.remove();
  const s = scenarioSwitcher(); s.id = 'scenSwitch';
  host.insertBefore(s, box);

  const v = S.results[S.current].validation, ss = v.soft_scores;
  const card = (label, value, unit, chip) => el('div', { class: 'panel' },
    el('h2', { text: label }),
    el('div', { class: 'body' },
      el('div', { class: 'big', text: value }, unit ? el('span', { class: 'unit', text: ' ' + unit }) : null),
      chip || null));
  box.append(
    card(T('objective'), (ss.objective_score !== undefined ? ss.objective_score : ss.priority_weighted_score), '',
      el('div', { style: 'margin-top:4px' },
        el('span', { class: 'chip ' + (v.feasible ? 'ok' : 'bad'),
                     text: v.feasible ? T('feasible') : `${v.hard_violations.length} ${T('viols')}` }))),
    card(T('overrun'), ss.overrun_days_total, 'd',
      el('p', { class: 'note', text: `${ss.contracts_overrunning} contracts` })),
    card(T('eclo') + ' / ' + T('excess'), `${ss.eclo_nights_total} / ${ss.excess_access_nights_total}`, '',
      el('p', { class: 'note', text: `${(v.detail && v.detail.nights_scheduled) ?? ''} ${T('nights')}` })));
}

function renderContracts() {
  const tb = $('#conTable tbody'); tb.textContent = '';
  const byNum = Object.fromEntries(S.detail.contracts.map(c => [c.number, c]));
  S.results[S.current].results.forEach(r => {
    const c = byNum[r.contract_number] || {};
    const ov = parseInt(r.overrun_days, 10);
    tb.append(el('tr', {},
      el('td', {}, el('b', { text: r.contract_number }), el('div', { class: 'note', text: c.description || '' })),
      el('td', { text: 'P' + (c.priority ?? '?') }),
      el('td', { text: c.planned || '' }),
      el('td', { text: r.simulated_completion_date }),
      el('td', { class: 'num' }, el('span', { class: 'chip ' + (ov > 0 ? 'bad' : 'ok'), text: ov > 0 ? '+' + ov + 'd' : 'on time' }))));
  });
}

function renderGantt() {
  const H = S.detail.summary.horizon_weeks;
  const g = $('#gantt'); g.textContent = '';
  g.style.setProperty('--wcell', (100 / H) + '%');
  const byAct = {};
  S.results[S.current].access.forEach(r => (byAct[r.activity_id] ||= []).push(r));
  const acts = Object.fromEntries(S.detail.activities.map(a => [a.id, a]));
  const cons = Object.fromEntries(S.detail.contracts.map(c => [c.number, c]));

  const hdr = el('div', { class: 'hdr' }, el('div', {}), (() => {
    const t = el('div', { class: 'track' });
    for (let w = 1; w <= H; w += 4)
      t.append(el('span', { text: 'w' + w, style: `left:${((w - 0.5) / H) * 100}%` }));
    return t;
  })());
  g.append(hdr);

  Object.keys(byAct).sort().forEach(id => {
    const a = acts[id] || {}, c = cons[a.contract] || {};
    const track = el('div', { class: 'track' });
    if (c.planned_week) track.append(el('div', { class: 'dl', style: `left:${(c.planned_week / H) * 100}%`, title: 'planned ' + c.planned }));
    byAct[id].forEach(r => {
      const w = parseInt(r.week, 10);
      const late = c.planned_week && w > c.planned_week;
      track.append(el('div', {
        class: 'cell' + (r.eclo === '1' ? ' eclo' : late ? ' late' : ''),
        style: `left:${((w - 1) / H) * 100}%;width:${(1 / H) * 100}%`,
        title: `${id} · week ${w}${r.eclo === '1' ? ' · ECLO' : ''} · night ${r.access_night}`
      }));
    });
    g.append(el('div', { class: 'lane' },
      el('div', { class: 'lab', text: `${id} ${a.contract || ''}`, title: id }), track));
  });
}

function renderActivities() {
  const tb = $('#actTable tbody'); tb.textContent = '';
  const byAct = {};
  S.results[S.current].access.forEach(r => (byAct[r.activity_id] ||= []).push(r));
  S.detail.activities.forEach(a => {
    const rows = byAct[a.id] || [];
    const units = rows.reduce((s, r) => s + (r.eclo === '1' ? 1.5 : 1), 0);
    const weeks = rows.map(r => +r.week).sort((x, y) => x - y);
    const tr = el('tr', { tabindex: '0' },
      el('td', { text: a.id }), el('td', { text: a.contract }),
      el('td', { class: 'num', text: a.total_accesses }),
      el('td', { class: 'num', text: units }),
      el('td', { text: weeks.length ? weeks.join(', ') : '—' }));
    const sel = () => { $$('#actTable tbody tr').forEach(x => x.classList.remove('sel')); tr.classList.add('sel'); showActivity(a, rows); };
    tr.addEventListener('click', sel);
    tr.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); sel(); } });
    tb.append(tr);
  });
}

function showActivity(a, rows) {
  S.selectedActivity = a.id;
  const d = $('#actDetail'); d.textContent = '';
  const c = S.detail.contracts.find(x => x.number === a.contract) || {};
  const dl = el('dl', { class: 'kv' });
  const add = (k, v) => dl.append(el('dt', { text: k }), el('dd', {}, v instanceof Node ? v : document.createTextNode(String(v))));
  add('Activity', a.id);
  add('Contract', `${a.contract} — ${c.description || ''} (P${c.priority}, ${c.nature}, ${c.access_type})`);
  add('Span', `${a.from} → ${a.to}`);
  add('Workload', `${a.total_accesses} access-nights`);
  add('Earliest week', `${a.earliest_week} (planned start ${a.planned_start})`);
  add('Predecessor', a.predecessor || '—');
  add('Scheduled weeks', rows.length ? rows.map(r => r.week + (r.eclo === '1' ? '*' : '')).join(', ') : '—');
  add('Books', `${a.occupied.length} locations`);
  add('Closure zone', `${a.closure.length} locations (buffer + mirroring)`);
  d.append(dl);
  d.append(el('h3', { text: 'Locations booked', style: 'margin-top:8px;font-size:.85rem' }));
  const ul = el('div', { class: 'note', style: 'font-family:ui-monospace,Menlo,monospace;font-size:.78rem' });
  ul.textContent = a.occupied.join('  ');
  d.append(ul);
  // "Why not earlier?" - ask the service to test one alternative placement.
  const whyWrap = el('div', { style: 'margin-top:10px;border-top:1px solid var(--rule-soft);padding-top:8px' });
  const wkIn = el('input', { type: 'number', min: '1', max: String(S.detail.summary.horizon_weeks),
                             value: String(Math.max(1, a.earliest_week)), style: 'width:64px' });
  const whyOut = el('pre', { class: 'log', style: 'max-height:200px;margin-top:6px' });
  whyOut.hidden = true;
  const askBtn = el('button', { class: 'btn', text: T('why_ask'), onclick: async () => {
    whyOut.hidden = false;
    whyOut.textContent = '…';
    const q = new URLSearchParams({ activity: a.id, week: wkIn.value,
                                    scenario: S.current || 'A', seconds: '15' });
    try {
      const r = await api(`/api/v1/instances/${S.instanceId}/explain?` + q.toString(), { method: 'POST' });
      whyOut.textContent = r.output || '(no output)';
    } catch (e) { whyOut.textContent = 'failed: ' + e.message; }
  } });
  whyWrap.append(el('h3', { text: T('why_title'), style: 'font-size:.85rem' }),
                 el('div', { class: 'row', style: 'margin-top:4px' },
                    el('label', { text: T('why_week') }), wkIn, askBtn),
                 el('p', { class: 'note', text: T('why_help') }), whyOut);

  const extra = a.closure.filter(x => !a.occupied.includes(x));
  if (extra.length) {
    d.append(el('h3', { text: 'Additionally closed by its buffer', style: 'margin-top:8px;font-size:.85rem' }));
    d.append(el('div', { class: 'note', style: 'font-family:ui-monospace,Menlo,monospace;font-size:.78rem', text: extra.join('  ') }));
  }
  d.append(whyWrap);
  drawNetwork();
}

// ---------------------------------------------------------------- 6 repair
const REP = [];
function fillRepairLocations() {
  const sel = $('#repLoc');
  if (sel.options.length || !S.detail) return;
  S.detail.locations.forEach(L =>
    sel.append(el('option', { value: L.id, text: `${L.id}  (supply ${L.supply})` })));
  $('#repFrom').max = $('#repTo').max = S.detail.summary.horizon_weeks;
}
function renderRepList() {
  const box = $('#repList'); box.textContent = '';
  REP.forEach((r, i) => box.append(el('div', {},
    el('span', { text: `${r.loc}  wk ${r.from}–${r.to}  →  ${r.sup} nights` }),
    el('button', { class: 'btn', text: '×', style: 'padding:0 8px',
                   onclick: () => { REP.splice(i, 1); renderRepList(); } }))));
  $('#repRun').disabled = REP.length === 0;
}
$('#repAdd').addEventListener('click', () => {
  const from = Math.max(1, +$('#repFrom').value), to = Math.max(from, +$('#repTo').value);
  REP.push({ loc: $('#repLoc').value, from, to, sup: Math.max(0, +$('#repSup').value) });
  renderRepList();
});
$('#repClear').addEventListener('click', () => { REP.length = 0; renderRepList(); $('#repOut').textContent = ''; });
$('#repRun').addEventListener('click', async () => {
  const q = new URLSearchParams({ scenario: S.current || 'A', seconds: '45' });
  REP.forEach(r => { for (let w = r.from; w <= r.to; w++) q.append('supply', `${r.loc}@${w}=${r.sup}`); });
  $('#repChip').innerHTML = '<span class="chip idle">re-planning…</span>';
  $('#repOut').textContent = '';
  try {
    const r = await api(`/api/v1/instances/${S.instanceId}/repair?` + q.toString(), { method: 'POST' });
    $('#repOut').textContent = r.output || '(no output)';
    const ok = r.exit === 0;
    $('#repChip').innerHTML = `<span class="chip ${ok ? 'ok' : 'bad'}">${ok ? 'repaired' : 'not repairable'}</span>`;
  } catch (e) {
    $('#repChip').innerHTML = `<span class="chip bad">${esc(e.message)}</span>`;
  }
});

// ---------------------------------------------------------------- network schematic
$('#wk').addEventListener('input', drawNetwork);
function drawNetwork() {
  if (!S.detail) return;
  const w = +$('#wk').value;
  $('#wkLabel').textContent = `week ${w} / ${S.detail.summary.horizon_weeks}`;
  const used = {};
  if (S.current) {
    const slots = {};
    S.results[S.current].occupancy.forEach(r => {
      if (+r.week !== w) return;
      (slots[r.location_id] ||= new Set()).add(r.co_share_group);
    });
    for (const [k, v] of Object.entries(slots)) used[k] = v.size;
  }
  const sel = S.selectedActivity ? S.detail.activities.find(a => a.id === S.selectedActivity) : null;
  const selOcc = new Set(sel ? sel.occupied : []);
  const selClo = new Set(sel ? sel.closure : []);

  const lines = ['ALP','BET'], bounds = ['EB','WB'];
  const byLine = {};
  S.detail.locations.forEach(L => ((byLine[L.line] ||= {})[L.bound] ||= []).push(L));
  for (const l of lines) for (const b of bounds) (byLine[l]?.[b] || []).sort((x, y) => x.chain - y.chain);

  const W = 1000, rowH = 52, top = 22;
  const svg = [`<svg class="net" viewBox="0 0 ${W} ${top + rowH * 4 + 16}" role="img" aria-label="Network occupancy schematic">`];
  let y = top;
  for (const l of lines) for (const b of bounds) {
    const arr = byLine[l]?.[b] || [];
    const n = arr.length;
    svg.push(`<text x="4" y="${y - 5}">${l} ${b}</text>`);
    arr.forEach((L, i) => {
      const x0 = 70 + (i / n) * (W - 80), x1 = 70 + ((i + 1) / n) * (W - 80);
      const u = used[L.id] || 0, cap = L.supply || 1;
      const ratio = Math.min(1, u / cap);
      let stroke = '#e0e0dc';
      if (u > 0) stroke = u >= cap ? '#a01c1c' : (ratio > 0.5 ? '#c07a1e' : '#1c5fa8');
      let sw = 3 + ratio * 6;
      if (selClo.has(L.id) && !selOcc.has(L.id)) stroke = '#c07a1e';
      if (selOcc.has(L.id)) { stroke = '#16467c'; sw = 11; }
      if (L.kind === 'sector') {
        svg.push(`<line class="seg" x1="${x0+1}" y1="${y+8}" x2="${x1-1}" y2="${y+8}" stroke="${stroke}" stroke-width="${sw}"><title>${esc(L.id)} — ${u}/${cap} nights</title></line>`);
      } else {
        svg.push(`<rect class="stn" x="${x0+1}" y="${y+2}" width="${Math.max(3,x1-x0-2)}" height="12" fill="${u>0?stroke:'#fff'}" stroke="#777"><title>${esc(L.id)} — ${u}/${cap} nights</title></rect>`);
        const st = L.id.split(':')[2];
        if (b === 'EB') svg.push(`<text x="${(x0+x1)/2}" y="${y-1}" text-anchor="middle" font-size="7">${esc(st)}</text>`);
      }
    });
    y += rowH;
  }
  svg.push('</svg>');
  $('#netSvg').innerHTML = svg.join('');
}

// ---------------------------------------------------------------- 5 check
function renderCheck() {
  const v = S.results[S.current].validation;
  const b = $('#chkBody'); b.textContent = '';
  b.append(scenarioSwitcher());
  b.append(el('p', {},
    el('span', { class: 'chip ' + (v.feasible ? 'ok' : 'bad'),
                 text: v.feasible ? T('feasible') : `${v.hard_violations.length} ${T('viols')}` })));
  const dl = el('dl', { class: 'kv' });
  const add = (k, x) => dl.append(el('dt', { text: k }), el('dd', { text: String(x) }));
  const ss = v.soft_scores;
  add(T('overrun'), ss.overrun_days_total);
  add(T('excess'), ss.excess_access_nights_total);
  add(T('eclo'), ss.eclo_nights_total);
  add('priority_weighted_score', ss.priority_weighted_score);
  add('priority_overrun', JSON.stringify(ss.priority_overrun));
  add('nights_scheduled', (v.detail && v.detail.nights_scheduled) ?? '');
  add('input_hash', (v.input_hash || '').slice(0, 24) + '…');
  add('checker', v.checker);
  b.append(dl);
  if (v.hard_violations.length) {
    const t = el('table', {}, el('thead', {}, el('tr', {}, el('th', { text: 'Rule' }), el('th', { text: 'Detail' }))));
    const tb = el('tbody');
    v.hard_violations.forEach(h => tb.append(el('tr', {}, el('td', { text: h.rule }),
      el('td', { text: h.detail, style: 'white-space:normal' }))));
    t.append(tb);
    b.append(el('div', { class: 'scroll', style: 'margin-top:8px' }, t));
  }
  b.append(el('p', { class: 'note', text: T('not_official') }));
  $('#chkJson').textContent = JSON.stringify(v, null, 2);
}

// ---------------------------------------------------------------- 6 export
function renderExport() {
  const b = $('#expBody'); b.textContent = '';
  if (!S.scenarios.length) { b.append(el('p', { class: 'note', text: T('none') })); return; }
  S.scenarios.forEach(sc => {
    const v = S.results[sc].validation;
    const box = el('div', { style: 'margin-bottom:12px' });
    box.append(el('h3', { text: 'Scenario ' + sc, style: 'font-size:.95rem' }));
    box.append(el('p', {},
      el('span', { class: 'chip ' + (v.feasible ? 'ok' : 'bad'),
                   text: v.feasible ? T('feasible') : `${v.hard_violations.length} ${T('viols')}` }),
      el('span', { class: 'note', text: `  ${T('objective')} ${v.soft_scores.objective_score ?? '—'}` })));
    const pv = S.results[sc].version;
    if (pv) {
      box.append(el('p', {},
        el('span', { class: 'chip ' + (pv.status === 'approved' ? 'ok' : 'idle'),
                     text: 'v' + pv.version_no + ' · ' + pv.status }),
        pv.status === 'approved'
          ? el('span', { class: 'note', text: `  approved by ${pv.approved_by} at ${pv.approved_at}` })
          : el('span', { class: 'note', text: '  ' + (pv.not_approvable_because || T('awaiting_approval')) })));
      if (pv.is_fallback)
        box.append(el('p', { class: 'note', style: 'color:#8a5300',
          text: T('fallback_warning') }));
    }
    if (!v.feasible)
      box.append(el('p', { class: 'note', style: 'color:#a01c1c',
        text: 'This plan did not pass the independent check and is not marked submission-ready.' }));
    const row = el('div', { class: 'row' });
    // Download the exact bytes the client already fetched and validated, rather
    // than re-requesting: a plain <a download> cannot carry the bearer token.
    ['SCHEDULE_ACCESS.csv','SCHEDULE_OCCUPANCY.csv','RESULTS.csv'].forEach(f => {
      const key = f.replace('.csv', '');
      row.append(el('button', { class: 'btn', text: T('dl') + ' ' + f, onclick: () => {
        const blob = new Blob([S.results[sc].raw[key]], { type: 'text/csv' });
        const url = URL.createObjectURL(blob);
        const a = el('a', { href: url, download: f });
        document.body.append(a); a.click(); a.remove();
        setTimeout(() => URL.revokeObjectURL(url), 5000);
      } }));
    });
    row.append(el('button', { class: 'btn', text: T('dl') + ' VALIDATION.json', onclick: () => {
      const blob = new Blob([JSON.stringify(S.results[sc].validation, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = el('a', { href: url, download: `VALIDATION_${sc}.json` });
      document.body.append(a); a.click(); a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 5000);
    } }));
    box.append(row);
    b.append(box);
  });
  b.append(el('p', { class: 'note', text: 'Files are served exactly as the worker wrote them; the figures above were computed by re-reading these bytes.' }));
}

// ---------------------------------------------------------------- boot
applyLang();
enableTabs(false);
renderPicked();
updateScenarioHelp();
boot().catch(e => showGate(String(e.message || e)));
