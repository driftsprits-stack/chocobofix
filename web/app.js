'use strict';
/* TrackAccess — browser client.
 *
 * Two rules this file follows:
 *   1. Everything on screen comes from the server. No figure is computed here
 *      that the server also computes, and nothing is shown as saved before the
 *      server has said so.
 *   2. Technical detail is available but never in the way. Solver status, raw
 *      reports and worker logs live behind "Show technical details".
 */

// ---------------------------------------------------------------- helpers
const $  = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

function el(tag, attrs = {}, ...kids) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') n.className = v;
    else if (k === 'text') n.textContent = v;
    else if (k === 'html') n.innerHTML = v;
    else if (k.startsWith('on') && typeof v === 'function') n.addEventListener(k.slice(2), v);
    else n.setAttribute(k, v === true ? '' : String(v));
  }
  for (const c of kids.flat()) {
    if (c == null || c === false) continue;
    n.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return n;
}
const esc = s => String(s ?? '').replace(/[<>&"]/g, c => ({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]));

function parseCsv(text) {
  const rows = []; let row = [], cur = '', q = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (q) {
      if (c === '"') { if (text[i+1] === '"') { cur += '"'; i++; } else q = false; }
      else cur += c;
    } else if (c === '"') q = true;
    else if (c === ',') { row.push(cur); cur = ''; }
    else if (c === '\n') { row.push(cur); cur=''; if (row.some(x=>x!=='')) rows.push(row); row = []; }
    else if (c !== '\r') cur += c;
  }
  row.push(cur);
  if (row.some(x => x !== '')) rows.push(row);
  if (!rows.length) return [];
  const head = rows[0].map(h => h.trim());
  return rows.slice(1).map(r => Object.fromEntries(head.map((h,i)=>[h,(r[i]??'').trim()])));
}

const FILES = ['01_LINES.csv','02_STATIONS.csv','03_SECTORS.csv','04_LOCATION_SUPPLY.csv',
               '05_BUFFER_LOCATION.csv','06_PARAMETERS.csv','07_PROJECT_DETAILS.csv',
               '08_ACTIVITY_DETAILS.csv'];
const SCENARIOS = ['A','B','C'];

// ---------------------------------------------------------------- state
const S = {
  lang: localStorage.getItem('ta.lang') || 'en',
  token: sessionStorage.getItem('ta.token') || '',
  user: null,
  view: 'projects',
  projects: [], project: null,
  instances: [], instanceId: null, detail: null,
  versions: [], scenario: 'A', results: {},
  job: null, poll: null,
  wizard: null,               // {stage, picked:Map, checkResult, jobId}
  selectedActivity: null,
  week: 1,
  compareTo: null,            // a version id to diff the schedule against
};
// An empty string is a legitimate translation (a column with no heading), so
// only fall back when the key is genuinely absent.
function T(k) {
  const L = window.I18N[S.lang];
  if (L && Object.prototype.hasOwnProperty.call(L, k)) return L[k];
  if (Object.prototype.hasOwnProperty.call(window.I18N.en, k)) return window.I18N.en[k];
  return k;
}

// ---------------------------------------------------------------- api
async function api(path, opts = {}) {
  const h = Object.assign({}, opts.headers || {});
  if (S.token) h['Authorization'] = 'Bearer ' + S.token;
  h['X-Correlation-Id'] = 'ui-' + Math.random().toString(36).slice(2, 10);
  let r;
  try {
    r = await fetch(path, Object.assign({}, opts, { headers: h }));
  } catch (e) {
    throw Object.assign(new Error(T('err_offline')), { offline: true });
  }
  const ct = r.headers.get('content-type') || '';
  const body = ct.includes('json') ? await r.json().catch(() => ({})) : await r.text();
  if (r.status === 401 && S.token) { signOutLocal(T('err_session')); }
  if (!r.ok) throw Object.assign(new Error((body && body.error) || r.statusText),
                                 { status: r.status, body });
  return body;
}

// ---------------------------------------------------------------- chrome
function fillLanguageSelects() {
  const names = { en: 'English', ms: 'Bahasa Melayu', zh: '简体中文', ta: 'தமிழ்' };
  for (const sel of [$('#langSel'), $('#langSignin')]) {
    if (!sel) continue;
    sel.textContent = '';
    for (const [code, label] of Object.entries(names))
      sel.append(el('option', { value: code, selected: code === S.lang }, label));
    sel.onchange = e => {
      S.lang = e.target.value;
      localStorage.setItem('ta.lang', S.lang);
      applyLang();
      render();                       // language never loses the task
    };
  }
}
function applyLang() {
  document.documentElement.lang = S.lang;
  $$('[data-i18n]').forEach(n => { const v = T(n.dataset.i18n); if (v) n.textContent = v; });
  fillLanguageSelects();
  renderChrome();
}
$('#sizeSel').addEventListener('change', e =>
  document.documentElement.style.setProperty('--fs', e.target.value + 'px'));

function renderChrome() {
  if (!S.user) return;
  const b = $('#acctBtn');
  b.textContent = S.user.username + ' · ' + T('role_' + S.user.role);
  b.onclick = () => openAccountMenu(b);
  const c = $('#ctx'); c.textContent = '';
  if (S.project) {
    c.append(el('b', { text: S.project.name }));
    if (S.detail) {
      c.append(el('span', { class: 'sep', text: '·' }),
               el('span', { text: `${S.detail.summary.activities} ${T('word_jobs')}` }),
               el('span', { class: 'sep', text: '·' }),
               el('span', { text: `${S.detail.summary.horizon_weeks} ${T('word_weeks')}` }));
    }
    const v = currentVersion();
    if (v) {
      c.append(el('span', { class: 'sep', text: '·' }),
               el('span', { text: T('word_scenario') + ' ' + v.scenario + ' v' + v.version_no }),
               statePill(v));
    }
  }
  const can = S.user.can;
  navBtn('overview').disabled = !S.project;
  navBtn('schedule').disabled = !S.project || !S.versions.length;
  navBtn('activities').disabled = !S.project || !S.detail;
  navBtn('history').disabled = !S.project;
  $$('#nav button').forEach(x => x.setAttribute('aria-current', x.dataset.view === S.view ? 'page' : 'false'));
  void can;
}
const navBtn = v => $(`#nav button[data-view="${v}"]`);

$$('#nav button').forEach(b => b.addEventListener('click', () => { if (!b.disabled) go(b.dataset.view); }));
function go(view) { S.view = view; render(); window.scrollTo(0, 0); }

function openAccountMenu(anchor) {
  closeDialogs();
  const menu = el('div', { class: 'card', role: 'menu',
    style: 'position:absolute;right:16px;top:52px;z-index:40;min-width:220px;box-shadow:var(--shadow-2)' },
    el('div', { class: 'body stack' },
      el('div', { class: 'small muted', text: S.user.username }),
      el('div', { class: 'small', text: T('role_' + S.user.role) + ' — ' + T('role_' + S.user.role + '_what') }),
      el('button', { class: 'btn', style: 'width:100%', onclick: () => { closeDialogs(); go('settings'); } }, T('nav_settings')),
      el('button', { class: 'btn', style: 'width:100%', onclick: () => { closeDialogs(); openHelp(); } }, T('help')),
      el('button', { class: 'btn btn-danger', style: 'width:100%', onclick: signOut }, T('signout'))));
  anchor.setAttribute('aria-expanded', 'true');
  $('#dialogHost').append(menu);
  menu.querySelector('button').focus();
  setTimeout(() => document.addEventListener('click', onceOutside, { once: true }), 0);
  function onceOutside(e) { if (!menu.contains(e.target)) closeDialogs(); }
}
function closeDialogs() {
  $('#dialogHost').textContent = '';
  const b = $('#acctBtn'); if (b) b.setAttribute('aria-expanded', 'false');
}
document.addEventListener('keydown', e => { if (e.key === 'Escape') closeDialogs(); });

// ---------------------------------------------------------------- pieces
function card(title, bodyNodes, headExtra) {
  const head = title ? el('header', {}, el('h2', { text: title }), headExtra || null) : null;
  return el('section', { class: 'card' }, head, el('div', { class: 'body' }, bodyNodes));
}
function cardFlush(title, nodes, headExtra) {
  return el('section', { class: 'card' },
    title ? el('header', {}, el('h2', { text: title }), headExtra || null) : null,
    el('div', { class: 'body flush' }, nodes));
}
function metric(label, value, note) {
  return el('div', { class: 'metric' },
    el('span', { class: 'value', text: value }),
    el('span', { class: 'label', text: label }),
    note ? el('span', { class: 'note', text: note }) : null);
}
function pill(kind, text) { return el('span', { class: 'pill pill-' + kind, text }); }

// A version's state in the planner's vocabulary, not the store's.
function stateOf(v) {
  if (!v) return { kind: 'idle', label: T('state_none') };
  if (v.status === 'approved')    return { kind: 'ok',   label: T('state_published') };
  if (v.status === 'superseded')  return { kind: 'idle', label: T('state_superseded') };
  if (v.status === 'invalidated') return { kind: 'bad',  label: T('state_outdated') };
  if (!v.feasible)                return { kind: 'bad',  label: T('state_failed') };
  return { kind: 'info', label: T('state_checked') };
}
const statePill = v => { const s = stateOf(v); return pill(s.kind, s.label); };

function table(headers, rows, opts = {}) {
  const thead = el('thead', {}, el('tr', {}, headers.map(h =>
    el('th', { class: h.num ? 'num' : null, scope: 'col', text: h.label }))));
  const tbody = el('tbody', {}, rows.length ? rows : el('tr', {},
    el('td', { colspan: headers.length, class: 'empty', text: opts.empty || T('nothing_here') })));
  return el('div', { class: 'table-wrap' }, el('table', {}, opts.caption ? el('caption', { text: opts.caption }) : null, thead, tbody));
}
function techDetails(label, node) {
  return el('details', { class: 'tech' }, el('summary', { text: label || T('show_tech') }), node);
}

// ---------------------------------------------------------------- sign in
function showSignin(msg) {
  $('#signin').classList.remove('hide');
  $('#app').classList.add('hide');
  const m = $('#signinMsg');
  m.textContent = '';
  if (msg) m.append(el('div', { class: 'callout callout-bad', style: 'margin-top:12px' },
                       el('span', { class: 'mark', text: '!' }), el('p', { text: msg })));
}
function hideSignin() { $('#signin').classList.add('hide'); $('#app').classList.remove('hide'); }

$('#signinForm').addEventListener('submit', async e => {
  e.preventDefault();
  const u = $('#su').value.trim(), p = $('#sp').value;
  if (!u || !p) return showSignin(T('err_need_both'));
  const btn = $('#signinGo');
  btn.disabled = true;
  try {
    const health = await api('/api/v1/health');
    const path = health.needs_bootstrap ? '/api/v1/bootstrap' : '/api/v1/auth/login';
    const q = new URLSearchParams({ username: u, password: p });
    const r = await api(path + '?' + q, { method: 'POST' });
    if (health.needs_bootstrap) {                    // first administrator: sign in next
      const r2 = await api('/api/v1/auth/login?' + q, { method: 'POST' });
      return enter(r2);
    }
    enter(r);
  } catch (err) {
    showSignin(err.message);
  } finally { btn.disabled = false; $('#sp').value = ''; }
});
$('#trySample').addEventListener('click', () => openDemoInfo());
$('#signinHelp').addEventListener('click', () => openHelp());

function enter(r) {
  S.token = r.token; S.user = r.user;
  sessionStorage.setItem('ta.token', S.token);
  hideSignin(); applyLang();
  loadProjects().then(() => go('projects'));
}
function signOutLocal(msg) {
  S.token = ''; S.user = null; S.project = null; S.versions = []; S.detail = null;
  sessionStorage.removeItem('ta.token');
  showSignin(msg || '');
}
async function signOut() {
  try { await api('/api/v1/auth/logout', { method: 'POST' }); } catch (e) {}
  closeDialogs(); signOutLocal('');
}

// "Try sample project" cannot mint an account, so explain honestly what to do.
function openDemoInfo() {
  openDialog(T('try_sample'), [
    el('p', { text: T('demo_p1') }),
    el('p', { text: T('demo_p2') }),
    el('pre', { class: 'log', text:
      'trackaccess-service --host 127.0.0.1 --port 8080 \\\n' +
      '  --root ./var --web ./web --worker ./build/trackaccess \\\n' +
      '  --public-instance ./data/upstream/PS1/01_data \\\n' +
      '  --bootstrap-admin "admin:choose-a-long-password"' }),
    el('p', { class: 'small muted', text: T('demo_p3') })]);
}
function openHelp() {
  openDialog(T('help'), [
    el('p', { text: T('help_p1') }),
    el('h3', { text: T('help_terms') }),
    el('dl', { class: 'detail' }, [
      ['term_possession', 'term_possession_d'], ['term_eclo', 'term_eclo_d'],
      ['term_cosharing', 'term_cosharing_d'], ['term_buffer', 'term_buffer_d'],
      ['term_checked', 'term_checked_d'], ['term_published', 'term_published_d'],
    ].flatMap(([k, d]) => [el('dt', { text: T(k) }), el('dd', { text: T(d) })])),
    el('p', { class: 'small muted', text: T('help_privacy') })]);
}
function openDialog(title, nodes) {
  closeDialogs();
  const close = el('button', { class: 'btn btn-sm', text: T('close') });
  const box = el('div', { class: 'card', role: 'dialog', 'aria-modal': 'true', 'aria-label': title,
    style: 'position:fixed;inset:auto 0 0 0;margin:0 auto 5vh;max-width:640px;max-height:80vh;overflow:auto;z-index:50;box-shadow:var(--shadow-2)' },
    el('header', {}, el('h2', { text: title }), close),
    el('div', { class: 'body stack' }, nodes));
  const veil = el('div', { style: 'position:fixed;inset:0;background:rgba(20,28,28,.28);z-index:49', onclick: closeDialogs });
  $('#dialogHost').append(veil, box);
  close.onclick = closeDialogs;
  close.focus();
}

// ---------------------------------------------------------------- data
async function loadProjects() {
  const r = await api('/api/v1/projects');
  S.projects = r.projects;
}
async function openProject(p) {
  S.project = p; S.instanceId = null; S.detail = null;
  S.versions = []; S.results = {}; S.selectedActivity = null; S.compareTo = null;
  await refreshVersions();
  const inst = await api(`/api/v1/projects/${p.id}/instances`);
  S.instances = inst.instances;
  if (S.instances.length) await useInstance(S.instances[0].id);
  go('overview');
}
async function refreshVersions() {
  if (!S.project) return;
  const r = await api(`/api/v1/projects/${S.project.id}/versions`);
  S.versions = r.versions;
  S.project.revision = r.revision;
}
async function useInstance(id) {
  S.instanceId = id;
  S.detail = await api(`/api/v1/instances/${id}/detail`);
  S.week = 1;
}
function currentVersion() {
  const forScen = S.versions.filter(v => v.scenario === S.scenario);
  return forScen.find(v => v.status === 'approved') || forScen[0] || null;
}
async function loadResult(v) {
  if (S.results[v.id]) return S.results[v.id];
  const [acc, occ, res, val] = await Promise.all([
    api(`/api/v1/versions/${v.id}/files/SCHEDULE_ACCESS.csv`),
    api(`/api/v1/versions/${v.id}/files/SCHEDULE_OCCUPANCY.csv`),
    api(`/api/v1/versions/${v.id}/files/RESULTS.csv`),
    api(`/api/v1/versions/${v.id}/validation`)]);
  S.results[v.id] = { access: parseCsv(acc), occupancy: parseCsv(occ), results: parseCsv(res),
                      validation: val, raw: { SCHEDULE_ACCESS: acc, SCHEDULE_OCCUPANCY: occ, RESULTS: res } };
  return S.results[v.id];
}

// ---------------------------------------------------------------- router
function render() {
  renderChrome();
  const v = $('#view'); v.textContent = '';
  const views = {
    projects: viewProjects, wizard: viewWizard, overview: viewOverview,
    schedule: viewSchedule, activities: viewActivities, history: viewHistory,
    settings: viewSettings,
  };
  (views[S.view] || viewProjects)(v);
}

// ---------------------------------------------------------------- projects
function viewProjects(root) {
  root.append(el('div', { class: 'page-head' },
    el('h1', { text: T('nav_projects') }),
    el('p', { text: T('projects_lede') })));

  const rows = S.projects.map(p => {
    const tr = el('tr', { tabindex: '0', role: 'button', 'aria-selected': S.project && S.project.id === p.id },
      el('td', {}, el('strong', { text: p.name })),
      el('td', { text: p.owner }),
      el('td', { text: p.created_at.slice(0, 10) }));
    const open = () => openProject(p).catch(showError);
    tr.onclick = open;
    tr.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(); } };
    return tr;
  });

  const list = cardFlush(T('projects_yours'),
    table([{ label: T('col_project') }, { label: T('col_owner') }, { label: T('col_created') }],
          rows, { empty: T('projects_empty') }));

  const side = [];
  if (S.user.can.create_project) {
    const name = el('input', { type: 'text', id: 'newProjectName', placeholder: T('projects_name_ph') });
    const msg = el('div', {});
    side.push(card(T('projects_new'), [
      el('p', { class: 'small muted', text: T('projects_new_help') }),
      el('div', { class: 'field' }, el('label', { for: 'newProjectName', text: T('col_project') }), name),
      el('div', { class: 'row' },
        el('button', { class: 'btn btn-primary', onclick: async () => {
          if (!name.value.trim()) return;
          try {
            const p = await api('/api/v1/projects?' + new URLSearchParams({ name: name.value.trim() }), { method: 'POST' });
            await loadProjects();
            await openProject({ id: p.id, name: p.name, owner: S.user.username, revision: p.revision, created_at: '' });
          } catch (e) { msg.textContent = ''; msg.append(errorNote(e)); }
        } }, T('projects_create'))),
      msg]));
  } else {
    side.push(card(T('projects_new'), el('p', { class: 'small muted', text: T('projects_readonly') })));
  }
  root.append(el('div', { class: 'grid cols-side' }, list, el('div', { class: 'stack' }, side)));
}
function errorNote(e) {
  return el('div', { class: 'callout callout-bad', style: 'margin-top:12px' },
    el('span', { class: 'mark', text: '!' }), el('p', { text: e.message }));
}
function showError(e) {
  const v = $('#view');
  v.prepend(errorNote(e));
}

// ---------------------------------------------- new plan: the four stages
const STAGES = ['upload', 'check', 'generate', 'review'];
function startWizard() {
  S.wizard = { stage: 'upload', picked: new Map(), checkResult: null, jobId: null, versions: [] };
  go('wizard');
}
function stageBar(current) {
  const bar = el('div', { class: 'stages', role: 'list' });
  STAGES.forEach((name, i) => {
    const idx = STAGES.indexOf(current);
    const state = i < idx ? 'done' : i === idx ? 'current' : 'todo';
    bar.append(el('div', { class: 'stage', role: 'listitem', 'data-state': state,
                           'aria-current': state === 'current' ? 'step' : null },
      el('span', { class: 'n', text: state === 'done' ? '✓' : String(i + 1) }),
      el('span', { text: T('stage_' + name) })));
    if (i < STAGES.length - 1) bar.append(el('span', { class: 'arrow', text: '→' }));
  });
  return bar;
}

function viewWizard(root) {
  if (!S.wizard) return startWizard();
  root.append(el('div', { class: 'page-head' },
    el('h1', { text: T('wizard_title') }),
    el('p', { text: T('wizard_lede') })));
  root.append(stageBar(S.wizard.stage));
  ({ upload: stageUpload, check: stageCheck, generate: stageGenerate, review: stageReview })
    [S.wizard.stage](root);
}

function stageUpload(root) {
  const w = S.wizard;
  const fileInput = el('input', { type: 'file', multiple: true, accept: '.csv', class: 'hide', id: 'fileInput' });
  const listBox = el('div', { class: 'filelist' });
  const status = el('div', {});

  function redraw() {
    listBox.textContent = '';
    FILES.forEach(f => {
      const got = w.picked.get(f);
      listBox.append(el('div', {},
        el('span', { class: 'mono', text: f }),
        got ? el('span', { class: 'got', text: '✓ ' + Math.round(got.size / 102.4) / 10 + ' kB' })
            : el('span', { class: 'miss', text: T('file_missing') })));
    });
    $('#toCheck').disabled = w.picked.size !== FILES.length;
    const n = w.picked.size;
    status.textContent = n ? `${n} ${T('of')} ${FILES.length} ${T('files_ready')}` : '';
  }
  // Files already chosen are kept when a later selection is partial or wrong.
  const add = list => {
    let unknown = 0;
    for (const f of list) { if (FILES.includes(f.name)) w.picked.set(f.name, f); else unknown++; }
    redraw();
    if (unknown) status.append(el('div', { class: 'small muted', text: T('files_ignored') }));
  };
  fileInput.onchange = e => add(e.target.files);

  const drop = el('div', { class: 'drop', tabindex: '0', role: 'button' },
    el('p', { text: T('upload_drop') }),
    el('button', { class: 'btn', onclick: () => fileInput.click() }, T('upload_choose')));
  drop.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); fileInput.click(); } };
  ['dragenter','dragover'].forEach(ev => drop.addEventListener(ev, e => { e.preventDefault(); drop.classList.add('over'); }));
  ['dragleave','drop'].forEach(ev => drop.addEventListener(ev, e => { e.preventDefault(); drop.classList.remove('over'); }));
  drop.addEventListener('drop', e => add(e.dataTransfer.files));

  const toCheck = el('button', { class: 'btn btn-primary', id: 'toCheck', disabled: true,
    onclick: () => doCheck(false) }, T('upload_continue'));
  const sample = el('button', { class: 'btn', onclick: () => doCheck(true) }, T('try_sample'));

  root.append(el('div', { class: 'grid cols-side' },
    card(T('upload_title'), [
      el('p', { text: T('upload_what') }),
      drop, fileInput, listBox, status,
      el('div', { class: 'row', style: 'margin-top:16px' }, toCheck, sample)]),
    card(T('upload_produces'), [
      el('p', { class: 'small', text: T('upload_produces_p') }),
      el('ul', { class: 'small muted' },
        el('li', { text: T('upload_out1') }), el('li', { text: T('upload_out2') }),
        el('li', { text: T('upload_out3') }))])));
  redraw();

  async function doCheck(useSample) {
    const box = el('div', { class: 'callout callout-info', style: 'margin-top:12px' },
      el('span', { class: 'mark', text: '…' }), el('p', { text: T('checking') }));
    status.append(box);
    toCheck.disabled = true; sample.disabled = true;
    try {
      let r;
      if (useSample) {
        r = await api(`/api/v1/projects/${S.project.id}/instances/demo`, { method: 'POST' });
      } else {
        const fd = new FormData();
        for (const [name, file] of w.picked) fd.append(name, file, name);
        r = await api(`/api/v1/projects/${S.project.id}/instances`, { method: 'POST', body: fd });
      }
      w.checkResult = { ok: true, data: r };
      await useInstance(r.instance_id);
      const inst = await api(`/api/v1/projects/${S.project.id}/instances`);
      S.instances = inst.instances;
      w.stage = 'check';
      render();
    } catch (e) {
      w.checkResult = { ok: false, errors: (e.body && e.body.errors) || [{ file: '', row: 0, field: '', message: e.message }] };
      w.stage = 'check';
      render();
    }
  }
}

function stageCheck(root) {
  const w = S.wizard, r = w.checkResult;
  if (!r) { w.stage = 'upload'; return render(); }

  if (!r.ok) {
    const rows = r.errors.map(e => el('tr', {},
      el('td', { class: 'mono', text: e.file || '—' }),
      el('td', { class: 'num', text: e.row || '' }),
      el('td', { class: 'mono', text: e.field || '' }),
      el('td', { text: e.message, style: 'white-space:normal' })));
    root.append(
      el('div', { class: 'callout callout-bad' }, el('span', { class: 'mark', text: '!' }),
        el('div', {}, el('p', { text: T('check_failed').replace('{n}', r.errors.length) }),
                      el('p', { class: 'small', text: T('check_failed_p') }))),
      cardFlush(T('check_problems'),
        table([{ label: T('col_file') }, { label: T('col_row'), num: true },
               { label: T('col_field') }, { label: T('col_problem') }], rows)),
      el('div', { class: 'row', style: 'margin-top:16px' },
        el('button', { class: 'btn btn-primary', onclick: () => { w.stage = 'upload'; render(); } }, T('check_fix')),
        el('button', { class: 'btn', onclick: () => go('projects') }, T('cancel'))));
    return;
  }

  const s = S.detail.summary;
  const contracts = S.detail.contracts;
  const firstStart = S.detail.activities.reduce((m, a) => a.planned_start < m ? a.planned_start : m,
                                                S.detail.activities[0].planned_start);
  const lastDue = contracts.reduce((m, c) => c.planned > m ? c.planned : m, contracts[0].planned);

  root.append(
    el('div', { class: 'callout callout-ok' }, el('span', { class: 'mark', text: '✓' }),
      el('div', {}, el('p', { text: T('check_ok') }),
        r.data.invalidated_plans
          ? el('p', { class: 'small', text: T('check_invalidated').replace('{n}', r.data.invalidated_plans) })
          : null)),
    el('div', { class: 'grid cols-3', style: 'margin:16px 0' },
      card(null, metric(T('word_jobs'), s.activities, T('check_jobs_note'))),
      card(null, metric(T('word_contracts'), s.contracts, T('check_contracts_note'))),
      card(null, metric(T('check_window'), `${s.horizon_weeks} ${T('word_weeks')}`,
                        `${s.horizon_start} → ${lastDue}`))),
    cardFlush(T('check_contracts_title'),
      table([{ label: T('col_contract') }, { label: T('col_priority') }, { label: T('col_kind') },
             { label: T('col_due') }, { label: T('col_nights'), num: true }],
        contracts.map(c => {
          const jobs = S.detail.activities.filter(a => a.contract === c.number);
          const nights = jobs.reduce((n, a) => n + a.total_accesses, 0);
          return el('tr', {},
            el('td', {}, el('strong', { text: c.number }), el('span', { class: 'sub', text: c.description })),
            el('td', {}, pill(c.priority === 1 ? 'bad' : c.priority === 2 ? 'warn' : 'idle', T('pri_' + c.priority))),
            el('td', { text: c.nature }),
            el('td', { text: c.planned }),
            el('td', { class: 'num', text: nights }));
        }))),
    el('div', { class: 'row', style: 'margin-top:16px' },
      el('button', { class: 'btn btn-primary', onclick: () => { w.stage = 'generate'; render(); } }, T('check_continue')),
      el('button', { class: 'btn', onclick: () => { w.stage = 'upload'; render(); } }, T('back')),
      el('span', { class: 'small muted', text: T('check_firststart') + ' ' + firstStart })));
}

function stageGenerate(root) {
  const w = S.wizard;
  const scenPick = el('div', { class: 'stack' });
  const chosen = { value: 'all' };
  const options = [
    ['all', 'scen_all', 'scen_all_d'],
    ['A', 'scen_A', 'scen_A_d'], ['B', 'scen_B', 'scen_B_d'], ['C', 'scen_C', 'scen_C_d']];
  options.forEach(([val, label, desc]) => {
    const id = 'scen_' + val;
    const input = el('input', { type: 'radio', name: 'scenario', id, value: val,
                                checked: val === chosen.value, style: 'width:auto' });
    input.onchange = () => { chosen.value = val; };
    scenPick.append(el('label', { for: id, class: 'callout',
                                  style: 'align-items:flex-start;cursor:pointer' },
      input, el('div', {}, el('strong', { text: T(label) }), el('p', { class: 'small muted', style: 'margin:4px 0 0', text: T(desc) }))));
  });

  const seconds = el('input', { type: 'number', id: 'budget', value: '60', min: '5', max: '120' });
  const advanced = el('details', { class: 'tech' },
    el('summary', { text: T('gen_advanced') }),
    el('div', { class: 'field', style: 'max-width:220px' },
      el('label', { for: 'budget', text: T('gen_budget') }), seconds,
      el('span', { class: 'hint', text: T('gen_budget_h') })));

  const progress = el('div', {});
  const runBtn = el('button', { class: 'btn btn-primary' }, T('gen_run'));
  const cancelBtn = el('button', { class: 'btn', disabled: true }, T('gen_stop'));

  root.append(el('div', { class: 'grid cols-side' },
    card(T('gen_title'), [el('p', { text: T('gen_lede') }), scenPick, advanced,
      el('div', { class: 'row', style: 'margin-top:16px' }, runBtn, cancelBtn)]),
    card(T('gen_rules'), [
      el('p', { class: 'small', text: T('gen_rules_p') }),
      el('ul', { class: 'small muted' },
        el('li', { text: T('gen_rule1') }), el('li', { text: T('gen_rule2') }),
        el('li', { text: T('gen_rule3') }), el('li', { text: T('gen_rule4') }))])));
  root.append(el('div', { style: 'margin-top:16px' }, progress));

  runBtn.onclick = async () => {
    runBtn.disabled = true; cancelBtn.disabled = false;
    progress.textContent = '';
    const live = el('div', { class: 'callout callout-info' },
      el('span', { class: 'mark', text: '…' }),
      el('div', {}, el('p', { id: 'genState', text: T('gen_queued') }),
                    el('p', { class: 'small muted', text: T('gen_truthful') })));
    const logBox = el('pre', { class: 'log', style: 'margin-top:12px' });
    progress.append(live, techDetails(T('show_tech'), logBox));
    try {
      const q = new URLSearchParams({ instance_id: S.instanceId, scenario: chosen.value,
                                      seconds: seconds.value, expected_revision: S.project.revision });
      const job = await api(`/api/v1/projects/${S.project.id}/jobs?` + q, { method: 'POST' });
      w.jobId = job.job_id;
      cancelBtn.onclick = async () => {
        cancelBtn.disabled = true;
        try { await api(`/api/v1/jobs/${w.jobId}/cancel`, { method: 'POST' }); } catch (e) {}
      };
      await pollJob(w.jobId, st => { $('#genState').textContent = T('gen_' + st) || st; },
                    txt => { logBox.textContent = txt; logBox.scrollTop = logBox.scrollHeight; });
      await refreshVersions();
      w.versions = S.versions.filter(v => (job.version_ids || []).includes(v.id));
      if (!w.versions.length) w.versions = S.versions.slice(0, 3);
      w.stage = 'review';
      render();
    } catch (e) {
      progress.textContent = '';
      if (e.status === 409 && e.body) {
        progress.append(el('div', { class: 'callout callout-warn' }, el('span', { class: 'mark', text: '!' }),
          el('div', {}, el('p', { text: T('gen_conflict') }),
            el('button', { class: 'btn', onclick: async () => { await refreshVersions(); render(); } }, T('reload')))));
      } else progress.append(errorNote(e));
      runBtn.disabled = false; cancelBtn.disabled = true;
    }
  };
}

// Polls a job. Reports only states the server reported; no percentage is
// invented, because the solver cannot say how far through it is.
async function pollJob(id, onState, onLog) {
  for (;;) {
    const j = await api(`/api/v1/jobs/${id}`);
    onState(j.state);
    try { onLog(await api(`/api/v1/jobs/${id}/log`)); } catch (e) {}
    if (['done','failed','cancelled'].includes(j.state)) {
      if (j.state === 'failed') throw new Error(j.error || T('gen_failed'));
      return j;
    }
    await new Promise(r => setTimeout(r, 700));
  }
}

async function stageReview(root) {
  const w = S.wizard;
  const versions = w.versions.length ? w.versions : S.versions;
  root.append(el('div', { id: 'reviewBody' }, el('p', { class: 'muted', text: T('loading') })));
  const body = $('#reviewBody');
  try {
    const loaded = [];
    for (const v of versions) loaded.push([v, await loadResult(v)]);
    body.textContent = '';

    // Say what happened before showing any table.
    const anyBad = loaded.some(([v]) => !v.feasible);
    body.append(el('div', { class: 'callout ' + (anyBad ? 'callout-bad' : 'callout-ok') },
      el('span', { class: 'mark', text: anyBad ? '!' : '✓' }),
      el('div', {},
        el('p', { text: anyBad ? T('review_bad') : T('review_ok').replace('{n}', loaded.length) }),
        el('p', { class: 'small', text: T('review_checked_means') }))));

    for (const [v, res] of loaded) {
      const ss = res.validation.soft_scores;
      const late = res.results.filter(r => +r.overrun_days > 0);
      body.append(el('section', { class: 'card', style: 'margin-top:16px' },
        el('header', {}, el('h2', { text: T('word_scenario') + ' ' + v.scenario + ' — ' + T('scen_' + v.scenario) }),
          statePill(v)),
        el('div', { class: 'body' },
          el('div', { class: 'grid cols-3' },
            card(null, metric(T('m_workload'), T('m_workload_ok'), T('m_workload_note'))),
            card(null, metric(T('m_late'), String(late.length), T('m_late_note'))),
            card(null, metric(T('m_extra'), `${ss.excess_access_nights_total} / ${ss.eclo_nights_total}`, T('m_extra_note')))),
          late.length ? el('div', { style: 'margin-top:16px' },
            table([{ label: T('col_contract') }, { label: T('col_due') },
                   { label: T('col_expected') }, { label: T('col_late'), num: true }],
              late.map(r => el('tr', { tabindex: '0', onclick: () => { S.scenario = v.scenario; go('schedule'); } },
                el('td', { text: r.contract_number }),
                el('td', { text: (S.detail.contracts.find(c => c.number === r.contract_number) || {}).planned || '' }),
                el('td', { text: r.simulated_completion_date }),
                el('td', { class: 'num' }, pill('warn', '+' + r.overrun_days + ' ' + T('word_days'))))),
              { caption: T('review_late_caption') }))
            : el('p', { class: 'small muted', style: 'margin-top:12px', text: T('review_none_late') }),
          techDetails(T('show_tech'),
            el('pre', { class: 'log', text: JSON.stringify(res.validation, null, 2) })))));
    }

    body.append(el('div', { class: 'row', style: 'margin-top:20px' },
      el('button', { class: 'btn btn-primary', onclick: () => { S.wizard = null; go('overview'); } }, T('review_done')),
      el('button', { class: 'btn', onclick: () => { w.stage = 'generate'; render(); } }, T('back')),
      el('span', { class: 'small muted', text: T('review_saved') })));
  } catch (e) { body.textContent = ''; body.append(errorNote(e)); }
}

// ---------------------------------------------------------------- overview
async function viewOverview(root) {
  root.append(el('div', { class: 'page-head' },
    el('h1', { text: S.project.name }),
    el('p', { text: T('overview_lede') })));

  if (!S.versions.length) {
    root.append(el('div', { class: 'card' }, el('div', { class: 'body empty' },
      el('h3', { text: T('overview_empty_h') }),
      el('p', { class: 'muted', text: T('overview_empty_p') }),
      S.user.can.run_solve
        ? el('button', { class: 'btn btn-primary', onclick: startWizard }, T('overview_start'))
        : el('p', { class: 'small muted', text: T('overview_need_planner') }))));
    return;
  }

  const byScen = {};
  SCENARIOS.forEach(sc => {
    const list = S.versions.filter(v => v.scenario === sc);
    byScen[sc] = list.find(v => v.status === 'approved') || list[0] || null;
  });

  root.append(el('div', { class: 'grid cols-3' }, SCENARIOS.map(sc => {
    const v = byScen[sc];
    return el('section', { class: 'card' },
      el('header', {}, el('h2', { text: T('word_scenario') + ' ' + sc }), statePill(v)),
      el('div', { class: 'body stack' },
        el('p', { class: 'small muted', style: 'margin:0', text: T('scen_' + sc) }),
        v ? metric(T('m_penalty'), v.objective, T('m_penalty_note')) : el('p', { class: 'muted', text: T('state_none') }),
        v ? el('div', { class: 'row tight' },
              el('button', { class: 'btn btn-sm', onclick: () => { S.scenario = sc; go('schedule'); } }, T('overview_open')),
              el('button', { class: 'btn btn-sm', onclick: () => { S.scenario = sc; go('history'); } }, T('overview_versions')))
          : null));
  })));

  const next = nextAction();
  if (next) root.append(el('div', { class: 'callout callout-info', style: 'margin-top:16px' },
    el('span', { class: 'mark', text: '→' }),
    el('div', {}, el('p', { text: next.text }), next.button)));

  if (S.user.can.run_solve)
    root.append(el('div', { class: 'row', style: 'margin-top:16px' },
      el('button', { class: 'btn', onclick: startWizard }, T('overview_newplan'))));
}
function nextAction() {
  const draft = S.versions.find(v => v.approvable);
  if (draft && S.user.can.approve)
    return { text: T('next_approve'), button: el('button', { class: 'btn btn-primary',
      onclick: () => { S.scenario = draft.scenario; go('history'); } }, T('overview_versions')) };
  if (draft)
    return { text: T('next_await'), button: null };
  const bad = S.versions.find(v => !v.feasible);
  if (bad) return { text: T('next_failed'), button: null };
  return null;
}

// ---------------------------------------------------------------- schedule
async function viewSchedule(root) {
  const v = currentVersion();
  if (!v) { go('overview'); return; }
  root.append(el('div', { class: 'page-head' },
    el('h1', { text: T('nav_schedule') }),
    el('p', { text: T('schedule_lede') })));

  const picker = el('div', { class: 'row', style: 'margin-bottom:16px' },
    el('span', { class: 'small muted', text: T('word_scenario') }),
    ...SCENARIOS.filter(sc => S.versions.some(x => x.scenario === sc)).map(sc =>
      el('button', { class: 'btn btn-sm' + (sc === S.scenario ? ' btn-primary' : ''),
        onclick: () => { S.scenario = sc; render(); } }, sc)),
    el('span', { class: 'spacer', style: 'flex:1' }),
    S.user.can.run_solve
      ? el('button', { class: 'btn btn-sm', onclick: openAdjust }, T('adjust'))
      : null);
  root.append(picker);

  const host = el('div', {}, el('p', { class: 'muted', text: T('loading') }));
  root.append(host);
  let res;
  try { res = await loadResult(v); } catch (e) { host.textContent=''; host.append(errorNote(e)); return; }
  host.textContent = '';

  const H = S.detail.summary.horizon_weeks;
  const weekLabel = el('span', { class: 'small muted' });
  const slider = el('input', { type: 'range', min: '1', max: String(H), value: String(S.week),
                               style: 'flex:1;max-width:420px', 'aria-label': T('word_week') });
  const svgHost = el('div', {});
  const drawNet = () => {
    S.week = +slider.value;
    weekLabel.textContent = `${T('word_week')} ${S.week} ${T('of')} ${H} · ${weekDate(S.week)}`;
    svgHost.innerHTML = networkSvg(res);
  };
  slider.oninput = drawNet;

  root.append(el('div', { class: 'grid cols-side' },
    el('div', { class: 'stack' },
      cardFlush(T('schedule_timeline'),
        el('div', { class: 'body' }, [
          el('div', { class: 'legend', style: 'margin-bottom:8px' },
            el('span', {}, el('i', {}), T('lg_night')),
            el('span', {}, el('i', { class: 'eclo' }), T('lg_eclo')),
            el('span', {}, el('i', { class: 'late' }), T('lg_late'))),
          timeline(res)])),
      card(T('schedule_network'), [
        el('div', { class: 'row' }, el('label', { class: 'small muted', text: T('word_week') }), slider, weekLabel),
        svgHost,
        el('p', { class: 'small muted', style: 'margin-top:8px', text: T('schedule_net_help') })])),
    el('div', { class: 'stack' },
      cardFlush(T('schedule_contracts'), contractTable(res)),
      card(T('schedule_detail'), el('div', { id: 'detailPanel' },
        el('p', { class: 'small muted', text: T('schedule_pick') }))))));
  drawNet();
}
function weekDate(w) {
  const start = new Date(S.detail.summary.horizon_start + 'T00:00:00Z');
  const d = new Date(start.getTime() + (w - 1) * 7 * 86400000);
  return d.toISOString().slice(0, 10);
}
function contractTable(res) {
  const byNum = Object.fromEntries(S.detail.contracts.map(c => [c.number, c]));
  return table([{ label: T('col_contract') }, { label: T('col_expected') }, { label: T('col_late'), num: true }],
    res.results.map(r => {
      const c = byNum[r.contract_number] || {};
      const late = +r.overrun_days;
      return el('tr', {},
        el('td', {}, el('strong', { text: r.contract_number }),
                     el('span', { class: 'sub', text: T('pri_' + (c.priority || 3)) })),
        el('td', { text: r.simulated_completion_date },
           el('span', { class: 'sub', text: T('col_due') + ' ' + (c.planned || '') })),
        el('td', { class: 'num' }, late > 0 ? pill('warn', '+' + late + ' ' + T('word_days'))
                                            : pill('ok', T('on_time'))));
    }));
}
function timeline(res) {
  const H = S.detail.summary.horizon_weeks;
  const acts = Object.fromEntries(S.detail.activities.map(a => [a.id, a]));
  const cons = Object.fromEntries(S.detail.contracts.map(c => [c.number, c]));
  const byAct = {};
  res.access.forEach(r => (byAct[r.activity_id] ||= []).push(r));

  const wrap = el('div', { class: 'timeline' });
  const head = el('div', { class: 'head' }, el('div', {}), (() => {
    const t = el('div', { class: 'track' });
    for (let w = 1; w <= H; w += 4)
      t.append(el('span', { style: `position:absolute;left:${((w - .5) / H) * 100}%;transform:translateX(-50%)`,
                            text: T('w_abbr') + w }));
    return t;
  })());
  wrap.append(head);

  Object.keys(byAct).sort().forEach(id => {
    const a = acts[id] || {}, c = cons[a.contract] || {};
    const track = el('div', { class: 'track' });
    for (let w = 5; w <= H; w += 5)
      track.append(el('div', { class: 'grid-line', style: `left:${((w - 1) / H) * 100}%` }));
    if (c.planned_week)
      track.append(el('div', { class: 'deadline', style: `left:${(c.planned_week / H) * 100}%`,
                               title: T('col_due') + ' ' + c.planned }));
    byAct[id].forEach(r => {
      const w = +r.week;
      const late = c.planned_week && w > c.planned_week;
      track.append(el('div', {
        class: 'bar' + (r.eclo === '1' ? ' eclo' : late ? ' late' : ''),
        style: `left:${((w - 1) / H) * 100}%;width:${(1 / H) * 100}%`,
        title: `${id} · ${T('word_week')} ${w} (${weekDate(w)})${r.eclo === '1' ? ' · ' + T('lg_eclo') : ''}` }));
    });
    const lane = el('div', { class: 'lane', tabindex: '0', role: 'button' },
      el('div', { class: 'who' }, el('span', { class: 'id', text: id }),
                                  el('span', { class: 'sub', text: a.contract || '' })),
      track);
    const pick = () => showActivity(id, res);
    lane.onclick = pick;
    lane.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); pick(); } };
    wrap.append(lane);
  });
  return wrap;
}
function networkSvg(res) {
  const used = {};
  const slots = {};
  res.occupancy.forEach(r => { if (+r.week === S.week) (slots[r.location_id] ||= new Set()).add(r.co_share_group); });
  for (const [k, v] of Object.entries(slots)) used[k] = v.size;

  const sel = S.selectedActivity ? S.detail.activities.find(a => a.id === S.selectedActivity) : null;
  const selOcc = new Set(sel ? sel.occupied : []);
  const selZone = new Set(sel ? sel.closure : []);

  const byLine = {};
  S.detail.locations.forEach(L => ((byLine[L.line] ||= {})[L.bound] ||= []).push(L));
  const lines = Object.keys(byLine).sort();
  for (const l of lines) for (const b of ['EB','WB']) (byLine[l][b] || []).sort((x,y) => x.chain - y.chain);

  const W = 1000, rowH = 54, top = 24;
  const rows = lines.flatMap(l => ['EB','WB'].map(b => [l, b]));
  const out = [`<svg class="net" viewBox="0 0 ${W} ${top + rowH * rows.length + 10}" role="img" aria-label="${esc(T('schedule_network'))}">`];
  let y = top;
  for (const [l, b] of rows) {
    const arr = byLine[l][b] || [];
    out.push(`<text x="4" y="${y - 6}" font-weight="600">${esc(l)} ${esc(b)}</text>`);
    arr.forEach((L, i) => {
      const x0 = 78 + (i / arr.length) * (W - 88), x1 = 78 + ((i + 1) / arr.length) * (W - 88);
      const u = used[L.id] || 0, cap = L.supply || 1;
      const ratio = Math.min(1, u / cap);
      let stroke = '#e2e7e5', wdt = 4;
      if (u > 0) { stroke = u >= cap ? '#a02420' : (ratio > .5 ? '#8a5600' : '#0d5f61'); wdt = 4 + ratio * 6; }
      if (selZone.has(L.id) && !selOcc.has(L.id)) { stroke = '#8a5600'; }
      if (selOcc.has(L.id)) { stroke = '#0a4a4c'; wdt = 12; }
      const title = `<title>${esc(L.id)} — ${u}/${cap} ${esc(T('word_nights'))}</title>`;
      if (L.kind === 'sector')
        out.push(`<line x1="${x0+1}" y1="${y+9}" x2="${x1-1}" y2="${y+9}" stroke="${stroke}" stroke-width="${wdt}" stroke-linecap="butt">${title}</line>`);
      else {
        out.push(`<rect x="${x0+1}" y="${y+2}" width="${Math.max(3,x1-x0-2)}" height="14" rx="2" fill="${u>0?stroke:'#fff'}" stroke="#b9c2c0">${title}</rect>`);
        if (b === 'EB') out.push(`<text x="${(x0+x1)/2}" y="${y-1}" text-anchor="middle" font-size="8">${esc(L.id.split(':')[2])}</text>`);
      }
    });
    y += rowH;
  }
  out.push('</svg>');
  return out.join('');
}

function showActivity(id, res) {
  S.selectedActivity = id;
  const a = S.detail.activities.find(x => x.id === id);
  const c = S.detail.contracts.find(x => x.number === a.contract) || {};
  const mine = res.access.filter(r => r.activity_id === id).sort((x,y) => +x.week - +y.week);
  const p = $('#detailPanel'); p.textContent = '';
  const dl = el('dl', { class: 'detail' });
  const add = (k, v) => dl.append(el('dt', { text: k }), el('dd', {}, v));
  add(T('col_job'), el('span', { class: 'id', text: a.id }));
  add(T('col_contract'), `${a.contract} — ${c.description || ''}`);
  add(T('d_where'), el('span', { class: 'mono small', text: `${a.from} → ${a.to}` }));
  add(T('d_workload'), `${a.total_accesses} ${T('word_nights')}`);
  add(T('d_earliest'), `${T('word_week')} ${a.earliest_week} (${a.planned_start})`);
  add(T('d_after'), a.predecessor || T('d_none'));
  add(T('d_scheduled'), mine.length ? mine.map(r => r.week + (r.eclo === '1' ? '*' : '')).join(', ') : '—');
  add(T('d_books'), `${a.occupied.length} ${T('d_locations')}`);
  p.append(el('div', { class: 'detail' }, dl));

  const extra = a.closure.filter(x => !a.occupied.includes(x));
  if (extra.length) p.append(el('p', { class: 'small muted', style: 'margin-top:12px',
                                       text: T('d_buffer').replace('{n}', extra.length) }));

  // "Why not another week?" — answered by the server, never guessed here.
  const wk = el('input', { type: 'number', min: '1', max: String(S.detail.summary.horizon_weeks),
                           value: String(Math.max(1, a.earliest_week)), style: 'width:80px' });
  const out = el('div', {});
  p.append(el('div', { style: 'margin-top:16px;border-top:1px solid var(--rule);padding-top:12px' },
    el('h3', { text: T('why_title') }),
    el('p', { class: 'small muted', text: T('why_help') }),
    el('div', { class: 'row tight' },
      el('label', { class: 'small', text: T('word_week') }), wk,
      el('button', { class: 'btn btn-sm', onclick: async () => {
        out.textContent = ''; out.append(el('p', { class: 'small muted', text: T('why_checking') }));
        try {
          const q = new URLSearchParams({ activity: id, week: wk.value, scenario: S.scenario, seconds: '15' });
          const r = await api(`/api/v1/instances/${S.instanceId}/explain?` + q, { method: 'POST' });
          out.textContent = '';
          out.append(explainSummary(r.output), techDetails(T('show_tech'), el('pre', { class: 'log', text: r.output })));
        } catch (e) { out.textContent = ''; out.append(errorNote(e)); }
      } }, T('why_check'))),
    out));
  drawNetworkAgain();
}
function drawNetworkAgain() {
  const v = currentVersion();
  if (!v || !S.results[v.id]) return;
  const host = $('svg.net');
  if (host && host.parentElement) host.parentElement.innerHTML = networkSvg(S.results[v.id]);
}
// Turns the worker's text into one sentence a planner can act on, keeping the
// three outcomes distinct. The full output stays available underneath.
function explainSummary(text) {
  if (/YES - it can/.test(text)) {
    const moved = (text.match(/(\d+) activities changed/) || [])[1];
    return el('div', { class: 'callout callout-ok' }, el('span', { class: 'mark', text: '✓' }),
      el('p', { text: T('why_yes') + (moved ? ' ' + T('why_moved').replace('{n}', moved) : '') }));
  }
  if (/NO - proven impossible/.test(text)) {
    const bind = (text.match(/BINDING\s+(.+)/) || [])[1];
    return el('div', { class: 'callout callout-bad' }, el('span', { class: 'mark', text: '✗' }),
      el('p', { text: T('why_no') + (bind ? ' ' + T('why_because') + ' ' + bind.trim() : '') }));
  }
  return el('div', { class: 'callout callout-warn' }, el('span', { class: 'mark', text: '?' }),
    el('p', { text: T('why_unknown') }));
}

// ---------------------------------------------------------------- activities
async function viewActivities(root) {
  root.append(el('div', { class: 'page-head' },
    el('h1', { text: T('nav_activities') }),
    el('p', { text: T('activities_lede') })));

  const v = currentVersion();
  let res = null;
  if (v) { try { res = await loadResult(v); } catch (e) {} }
  const byAct = {};
  if (res) res.access.forEach(r => (byAct[r.activity_id] ||= []).push(r));

  const search = el('input', { type: 'search', placeholder: T('activities_search'), style: 'max-width:280px' });
  const tbody = el('tbody');
  const draw = () => {
    const q = search.value.trim().toLowerCase();
    tbody.textContent = '';
    const rows = S.detail.activities.filter(a =>
      !q || a.id.toLowerCase().includes(q) || a.contract.toLowerCase().includes(q) ||
      a.from.toLowerCase().includes(q));
    if (!rows.length) { tbody.append(el('tr', {}, el('td', { colspan: 6, class: 'empty', text: T('nothing_here') }))); return; }
    rows.forEach(a => {
      const mine = byAct[a.id] || [];
      const units = mine.reduce((s, r) => s + (r.eclo === '1' ? 1.5 : 1), 0);
      const weeks = mine.map(r => +r.week).sort((x,y) => x - y);
      const tr = el('tr', { tabindex: '0', role: 'button' },
        el('td', { class: 'id', text: a.id }),
        el('td', { text: a.contract }),
        el('td', { class: 'mono small', text: a.from.replace(/^SEC:/, '') }),
        el('td', { class: 'num', text: a.total_accesses }),
        el('td', { class: 'num', text: res ? units : '—' }),
        el('td', { text: weeks.length ? weeks.join(', ') : T('d_unscheduled') }));
      const pick = () => { if (res) { S.scenario = v.scenario; go('schedule'); setTimeout(() => showActivity(a.id, res), 0); } };
      tr.onclick = pick;
      tr.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); pick(); } };
      tbody.append(tr);
    });
  };
  search.oninput = draw;

  root.append(cardFlush(T('activities_all'),
    el('div', {},
      el('div', { class: 'body', style: 'padding-bottom:0' }, el('div', { class: 'row' }, search)),
      el('div', { class: 'table-wrap' },
        el('table', {},
          el('thead', {}, el('tr', {},
            el('th', { scope: 'col', text: T('col_job') }), el('th', { scope: 'col', text: T('col_contract') }),
            el('th', { scope: 'col', text: T('col_where') }), el('th', { class: 'num', scope: 'col', text: T('col_needs') }),
            el('th', { class: 'num', scope: 'col', text: T('col_given') }), el('th', { scope: 'col', text: T('col_weeks') }))),
          tbody)))));
  draw();
}

// ---------------------------------------------------------------- history
async function viewHistory(root) {
  root.append(el('div', { class: 'page-head' },
    el('h1', { text: T('nav_history') }),
    el('p', { text: T('history_lede') })));

  await refreshVersions();
  const rows = S.versions.map(v => {
    const st = stateOf(v);
    const act = el('div', { class: 'row tight' });
    if (S.user.can.approve && v.approvable) {
      act.append(el('button', { class: 'btn btn-sm btn-primary', onclick: () => confirmPublish(v) }, T('publish')));
    } else if (S.user.can.approve && v.status === 'approved') {
      act.append(el('button', { class: 'btn btn-sm btn-danger', onclick: () => confirmUnpublish(v) }, T('unpublish')));
    } else if (v.not_approvable_because) {
      act.append(el('span', { class: 'small muted', text: v.not_approvable_because }));
    }
    act.append(el('button', { class: 'btn btn-sm', onclick: () => openDownloads(v) }, T('download')));
    return el('tr', {},
      el('td', {}, el('strong', { text: T('word_scenario') + ' ' + v.scenario + ' v' + v.version_no }),
                   el('span', { class: 'sub id', text: v.content_hash.slice(0, 12) })),
      el('td', {}, pill(st.kind, st.label),
        v.is_fallback ? pill('warn', T('flag_fallback')) : null,
        v.strict_buffers ? pill('idle', T('flag_strict')) : null),
      el('td', { class: 'num', text: v.objective }),
      el('td', {}, v.created_by, el('span', { class: 'sub', text: v.created_at.slice(0, 16).replace('T', ' ') })),
      el('td', {}, v.approved_by || '—',
        v.approved_at ? el('span', { class: 'sub', text: v.approved_at.slice(0, 16).replace('T',' ') }) : null),
      el('td', {}, act));
  });

  root.append(cardFlush(T('history_versions'),
    table([{ label: T('col_version') }, { label: T('col_state') }, { label: T('col_penalty'), num: true },
           { label: T('col_madeby') }, { label: T('col_publishedby') }, { label: T('col_action') }],
          rows, { empty: T('history_empty') })));
  root.append(el('p', { class: 'small muted', style: 'margin-top:8px', text: T('history_immutable') }));

  if (S.user.can.view_audit) {
    const box = el('div', {}, el('p', { class: 'muted', text: T('loading') }));
    root.append(el('div', { style: 'margin-top:16px' }, cardFlush(T('history_audit'), box)));
    try {
      const a = await api(`/api/v1/projects/${S.project.id}/audit`);
      box.textContent = '';
      box.append(table([{ label: T('col_when') }, { label: T('col_who') }, { label: T('col_what') },
                        { label: T('col_result') }, { label: T('col_detail') }],
        a.events.slice(0, 120).map(e => el('tr', {},
          el('td', { class: 'small', text: e.ts.replace('T',' ').replace('Z','') }),
          el('td', { text: e.actor || '—' }),
          el('td', { class: 'small', text: T('audit_' + e.action) || e.action }),
          el('td', {}, pill(e.result === 'ok' ? 'ok' : (e.result === 'denied' || e.result === 'refused') ? 'bad' : 'idle', e.result)),
          el('td', { class: 'small muted', style: 'white-space:normal', text: e.detail })))));
    } catch (e) { box.textContent = ''; box.append(errorNote(e)); }
  }
}
function confirmPublish(v) {
  openDialog(T('publish_title'), [
    el('p', { text: T('publish_p1').replace('{s}', v.scenario).replace('{v}', v.version_no) }),
    el('p', { class: 'small muted', text: T('publish_p2') }),
    el('dl', { class: 'detail' },
      el('dt', { text: T('col_penalty') }), el('dd', { text: v.objective }),
      el('dt', { text: T('publish_plan') }), el('dd', { class: 'id', text: v.content_hash.slice(0, 16) }),
      el('dt', { text: T('publish_check') }), el('dd', { class: 'id', text: v.validation_hash.slice(0, 16) })),
    el('div', { class: 'row end' },
      el('button', { class: 'btn', onclick: closeDialogs }, T('cancel')),
      el('button', { class: 'btn btn-primary', onclick: async () => {
        try {
          await api(`/api/v1/versions/${v.id}/approve?` +
            new URLSearchParams({ content_hash: v.content_hash, validation_hash: v.validation_hash }),
            { method: 'POST' });
          closeDialogs(); await refreshVersions(); render();
        } catch (e) { closeDialogs(); openDialog(T('publish_failed'), [el('p', { text: e.message })]); }
      } }, T('publish')))]);
}
function confirmUnpublish(v) {
  const why = el('input', { type: 'text', placeholder: T('unpublish_why_ph') });
  openDialog(T('unpublish_title'), [
    el('p', { text: T('unpublish_p') }),
    el('div', { class: 'field' }, el('label', { text: T('unpublish_why') }), why),
    el('div', { class: 'row end' },
      el('button', { class: 'btn', onclick: closeDialogs }, T('cancel')),
      el('button', { class: 'btn btn-danger', onclick: async () => {
        try {
          await api(`/api/v1/versions/${v.id}/revoke?` + new URLSearchParams({ reason: why.value }), { method: 'POST' });
          closeDialogs(); await refreshVersions(); render();
        } catch (e) { closeDialogs(); showError(e); }
      } }, T('unpublish')))]);
}
async function openDownloads(v) {
  const res = await loadResult(v).catch(() => null);
  const files = ['SCHEDULE_ACCESS','SCHEDULE_OCCUPANCY','RESULTS'];
  openDialog(T('download_title'), [
    el('p', { text: T('download_p').replace('{s}', v.scenario).replace('{v}', v.version_no) }),
    !v.feasible ? el('div', { class: 'callout callout-bad' }, el('span', { class: 'mark', text: '!' }),
      el('p', { text: T('download_bad') })) : null,
    v.is_fallback ? el('div', { class: 'callout callout-warn' }, el('span', { class: 'mark', text: '!' }),
      el('p', { text: T('download_fallback') })) : null,
    el('div', { class: 'row' }, files.map(f => el('button', { class: 'btn', onclick: () => {
      if (!res) return;
      const blob = new Blob([res.raw[f]], { type: 'text/csv' });
      const url = URL.createObjectURL(blob);
      const a = el('a', { href: url, download: f + '.csv' });
      document.body.append(a); a.click(); a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 4000);
    } }, f + '.csv')))]);
}

// ------------------------------------------------- adjust (disruption repair)
function openAdjust() {
  const loc = el('select', {});
  S.detail.locations.forEach(L => loc.append(el('option', { value: L.id },
    `${L.id}  (${L.supply} ${T('word_nights')})`)));
  const from = el('input', { type: 'number', min: '1', max: String(S.detail.summary.horizon_weeks), value: String(S.week) });
  const to   = el('input', { type: 'number', min: '1', max: String(S.detail.summary.horizon_weeks), value: String(S.week) });
  const left = el('input', { type: 'number', min: '0', value: '0' });
  const out  = el('div', {});

  openDialog(T('adjust_title'), [
    el('p', { text: T('adjust_p') }),
    el('div', { class: 'field' }, el('label', { text: T('adjust_where') }), loc),
    el('div', { class: 'row' },
      el('div', { class: 'field', style: 'flex:1' }, el('label', { text: T('adjust_from') }), from),
      el('div', { class: 'field', style: 'flex:1' }, el('label', { text: T('adjust_to') }), to),
      el('div', { class: 'field', style: 'flex:1' }, el('label', { text: T('adjust_left') }), left)),
    el('div', { class: 'row' },
      el('button', { class: 'btn btn-primary', onclick: async () => {
        out.textContent = '';
        out.append(el('div', { class: 'callout callout-info' }, el('span', { class: 'mark', text: '…' }),
                      el('p', { text: T('adjust_working') })));
        const q = new URLSearchParams({ scenario: S.scenario, seconds: '45' });
        const a = Math.max(1, +from.value), b = Math.max(a, +to.value);
        for (let w = a; w <= b; w++) q.append('supply', `${loc.value}@${w}=${Math.max(0, +left.value)}`);
        try {
          const r = await api(`/api/v1/instances/${S.instanceId}/repair?` + q, { method: 'POST' });
          out.textContent = '';
          out.append(adjustSummary(r.output, r.exit),
                     techDetails(T('show_tech'), el('pre', { class: 'log', text: r.output })));
        } catch (e) { out.textContent = ''; out.append(errorNote(e)); }
      } }, T('adjust_preview')),
      el('button', { class: 'btn', onclick: closeDialogs }, T('cancel'))),
    out,
    el('p', { class: 'small muted', text: T('adjust_note') })]);
}
function adjustSummary(text, exit) {
  if (exit !== 0)
    return el('div', { class: 'callout callout-bad' }, el('span', { class: 'mark', text: '!' }),
      el('p', { text: T('adjust_impossible') }));
  const obj = [...text.matchAll(/objective ([0-9.]+)/g)].map(m => m[1]);
  const moved = (text.match(/(\d+) activities changed/) || [])[1];
  const churn = (text.match(/churn (\d+)/) || [])[1];
  return el('div', { class: 'callout callout-warn' }, el('span', { class: 'mark', text: '→' }),
    el('div', {},
      el('p', { text: T('adjust_result')
        .replace('{before}', obj[0] ?? '?').replace('{after}', obj[1] ?? '?')
        .replace('{n}', moved ?? '?') }),
      el('p', { class: 'small muted', text: T('adjust_churn').replace('{n}', churn ?? '?') })));
}

// ---------------------------------------------------------------- settings
async function viewSettings(root) {
  root.append(el('div', { class: 'page-head' },
    el('h1', { text: T('nav_settings') }), el('p', { text: T('settings_lede') })));

  const cards = [
    card(T('settings_account'), [
      el('dl', { class: 'detail' },
        el('dt', { text: T('col_who') }), el('dd', { text: S.user.username }),
        el('dt', { text: T('settings_role') }), el('dd', { text: T('role_' + S.user.role) }),
        el('dt', { text: T('settings_may') }), el('dd', { text: T('role_' + S.user.role + '_what') })),
      el('div', { class: 'row', style: 'margin-top:12px' },
        el('button', { class: 'btn btn-danger', onclick: signOut }, T('signout')))]),
    card(T('settings_privacy'), [
      el('p', { class: 'small', text: T('privacy_p1') }),
      el('ul', { class: 'small muted' },
        el('li', { text: T('privacy_l1') }), el('li', { text: T('privacy_l2') }),
        el('li', { text: T('privacy_l3') }), el('li', { text: T('privacy_l4') })),
      el('p', { class: 'small muted', text: T('privacy_p2') })]),
  ];

  if (S.user.can.manage_users) {
    const box = el('div', {}, el('p', { class: 'muted', text: T('loading') }));
    cards.push(cardFlush(T('settings_accounts'), box));
    try {
      const r = await api('/api/v1/users');
      box.textContent = '';
      box.append(table([{ label: T('col_who') }, { label: T('settings_role') }, { label: T('col_state') }, { label: T('col_action') }],
        r.users.map(u => el('tr', {},
          el('td', { text: u.username }), el('td', { text: T('role_' + u.role) }),
          el('td', {}, pill(u.disabled ? 'bad' : 'ok', u.disabled ? T('acct_disabled') : T('acct_active'))),
          el('td', {}, u.id === S.user.id ? el('span', { class: 'small muted', text: T('acct_you') })
            : el('button', { class: 'btn btn-sm', onclick: async () => {
                await api(`/api/v1/users/${u.id}/disable?disabled=${u.disabled ? '0' : '1'}`, { method: 'POST' });
                render();
              } }, u.disabled ? T('acct_enable') : T('acct_disable')))))));
      const nu = el('input', { type: 'text' }), np = el('input', { type: 'password' });
      const nr = el('select', {}, ['planner','approver','viewer','administrator']
        .map(x => el('option', { value: x }, T('role_' + x))));
      const msg = el('div', {});
      box.append(el('div', { class: 'body' },
        el('h3', { text: T('acct_add') }),
        el('div', { class: 'row' },
          el('div', { class: 'field', style: 'flex:1' }, el('label', { text: T('username') }), nu),
          el('div', { class: 'field', style: 'flex:1' }, el('label', { text: T('password') }), np),
          el('div', { class: 'field', style: 'flex:1' }, el('label', { text: T('settings_role') }), nr)),
        el('div', { class: 'row' }, el('button', { class: 'btn btn-primary', onclick: async () => {
          try {
            await api('/api/v1/users?' + new URLSearchParams({ username: nu.value.trim(), password: np.value, role: nr.value }), { method: 'POST' });
            render();
          } catch (e) { msg.textContent = ''; msg.append(errorNote(e)); }
        } }, T('acct_create'))),
        el('p', { class: 'small muted', text: T('acct_note') }), msg));
    } catch (e) { box.textContent = ''; box.append(errorNote(e)); }
  }
  root.append(el('div', { class: 'grid cols-2' }, cards));
}
$('#helpBtn').addEventListener('click', openHelp);

// ---------------------------------------------------------------- boot
(async function boot() {
  applyLang();
  try {
    const h = await api('/api/v1/health');
    if (h.needs_bootstrap) {
      $('#signinNote').textContent = T('signin_first');
      $('#signinGo').textContent = T('create_admin');
      showSignin('');
      return;
    }
  } catch (e) { showSignin(T('err_offline')); return; }
  if (S.token) {
    try {
      const me = await api('/api/v1/auth/me');
      S.user = me.user;
      hideSignin(); applyLang();
      await loadProjects();
      go('projects');
      return;
    } catch (e) { /* fall through */ }
  }
  showSignin('');
})();
