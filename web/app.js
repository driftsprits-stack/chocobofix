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
  token: new URLSearchParams(location.search).get('token') || sessionStorage.getItem('ta.token') || '',
  instanceId: null, detail: null, jobId: null, poll: null,
  picked: new Map(), results: {}, scenarios: [], current: null, selectedActivity: null
};
if (S.token) sessionStorage.setItem('ta.token', S.token);

const T = k => (window.I18N[S.lang] && window.I18N[S.lang][k]) || window.I18N.en[k] || k;

async function api(path, opts = {}) {
  const h = Object.assign({}, opts.headers || {});
  if (S.token) h['Authorization'] = 'Bearer ' + S.token;
  const r = await fetch(path, Object.assign({}, opts, { headers: h }));
  const ct = r.headers.get('content-type') || '';
  const body = ct.includes('json') ? await r.json() : await r.text();
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
  ['import','solve','schedule','network','check','repair','export']
    .forEach(t => $('#p-' + t).classList.toggle('hide', t !== name));
  if (name === 'network') drawNetwork();
  if (name === 'repair') fillRepairLocations();
}
$$('#tabs button').forEach(b => b.addEventListener('click', () => { if (!b.disabled) showTab(b.dataset.tab); }));
const enableTabs = on => ['solve','schedule','network','check','repair','export']
  .forEach(t => { $(`#tabs button[data-tab="${t}"]`).disabled = !on; });

// ---------------------------------------------------------------- 1 import
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
async function afterLoad(res) {
  S.instanceId = res.instance_id;
  S.detail = await api(`/api/v1/instances/${S.instanceId}/detail`);
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
  $('#instMeta').innerHTML = `<b>${esc(s.activities)}</b> activities · <b>${esc(s.contracts)}</b> contracts · ${esc(s.horizon_weeks)} wk · <code>${esc(s.input_hash.slice(0,8))}</code>`;
  $('#wk').max = s.horizon_weeks;
  enableTabs(true);
  $('#impStatus').innerHTML = `<span class="chip ok">accepted</span>`;
  showTab('solve');
}
$('#upload').addEventListener('click', async () => {
  $('#impStatus').innerHTML = '<span class="chip idle">checking…</span>';
  const fd = new FormData();
  for (const [name, file] of S.picked) fd.append(name, file, name);
  try {
    await afterLoad(await api('/api/v1/instances', { method: 'POST', body: fd }));
  } catch (e) {
    $('#impStatus').innerHTML = '<span class="chip bad">rejected</span>';
    if (e.body && e.body.errors) showImportErrors(e.body.errors);
    else showImportErrors([{ file: '', row: 0, field: '', message: e.message }]);
  }
});
$('#demo').addEventListener('click', async () => {
  $('#impStatus').innerHTML = '<span class="chip idle">loading…</span>';
  try { await afterLoad(await api('/api/v1/instances/demo', { method: 'POST' })); }
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
    instance_id: S.instanceId, scenario: $('#scen').value, seconds: $('#secs').value
  });
  try {
    const job = await api('/api/v1/jobs?' + p.toString(), { method: 'POST' });
    S.jobId = job.job_id;
    $('#run').disabled = true; $('#cancel').disabled = false;
    $('#jobChip').innerHTML = '<span class="chip idle">queued</span>';
    S.poll = setInterval(pollJob, 700);
  } catch (e) {
    $('#jobChip').innerHTML = `<span class="chip bad">${esc(e.message)}</span>`;
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
  const want = $('#scen').value === 'all' ? ['A','B','C'] : [$('#scen').value];
  S.results = {}; S.scenarios = [];
  for (const sc of want) {
    try {
      const [acc, occ, rs, val] = await Promise.all([
        api(`/api/v1/jobs/${S.jobId}/files/${sc}/SCHEDULE_ACCESS.csv`),
        api(`/api/v1/jobs/${S.jobId}/files/${sc}/SCHEDULE_OCCUPANCY.csv`),
        api(`/api/v1/jobs/${S.jobId}/files/${sc}/RESULTS.csv`),
        api(`/api/v1/jobs/${S.jobId}/validation/${sc}`)
      ]);
      S.results[sc] = {
        access: parseCsv(acc), occupancy: parseCsv(occ), results: parseCsv(rs),
        validation: val, raw: { SCHEDULE_ACCESS: acc, SCHEDULE_OCCUPANCY: occ, RESULTS: rs }
      };
      S.scenarios.push(sc);
    } catch (e) { /* scenario not produced */ }
  }
  if (S.scenarios.length) { S.current = S.scenarios[0]; renderAll(); showTab('schedule'); }
}

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
