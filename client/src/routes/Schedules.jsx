import { useEffect, useMemo, useRef, useState } from 'react';
import { Navigate, useSearchParams, Link } from 'react-router-dom';
import { useProjectIndex } from '../lib/useProjectIndex.js';
import ProjectPages from '../components/ProjectPages.jsx';
import Shell from '../components/Shell.jsx';
import { Loading, Failed, Empty } from '../components/state.jsx';
import { useAuth } from '../state/auth.jsx';
import { useResource } from '../lib/useResource.js';
import { api } from '../lib/api.js';
import { useProjectSchedule } from '../lib/useProjectSchedule.js';
import { planState } from '../lib/planChoice.js';
import { formatWeek } from '../lib/scenarios.js';
import { filterRows, groupRows, activityRows, sortRows } from '../lib/scheduleModel.js';
import ActivityDetail from '../components/ActivityDetail.jsx';
import Avatar from '../components/Avatar.jsx';

const LIST_PAGE_SIZE = 25;
const LINE_NAMES = { ALP: 'Alpha line', BET: 'Beta line' };
const DIRECTION_NAMES = { EB: 'eastbound', WB: 'westbound', NB: 'northbound', SB: 'southbound' };

function scheduleWeek(horizonStart, week) {
  const full = formatWeek(horizonStart, week);
  const [, date] = full.split('·');
  return { week: `week ${week}`, date: date?.trim() || 'date not supplied' };
}

function readableLocation(value) {
  if (!value) return { route: 'location not supplied', context: '', code: '' };
  const parts = String(value).split(':');
  if (parts.length < 4) return { route: value, context: '', code: value };
  const [, line, span, direction] = parts;
  const [from, to] = span.split('_');
  return {
    route: to ? `${from} → ${to}` : span,
    context: [LINE_NAMES[line] || line, DIRECTION_NAMES[direction] || direction].filter(Boolean).join(' · '),
    code: value,
  };
}

// A slot is compared with its contract's planned completion week. This keeps
// the colour meaningful across uploaded datasets instead of hard-coding dates.
function timingStatus(row) {
  const target = Number(row.contractInfo?.planned_week);
  if (!Number.isFinite(target) || target < 1) return { key: 'unknown', label: 'target not supplied' };
  if (row.week < target) return { key: 'early', label: 'before target' };
  if (row.week === target) return { key: 'on-time', label: 'target week' };
  const weeksLate = row.week - target;
  return { key: 'late', label: `${weeksLate} ${weeksLate === 1 ? 'week' : 'weeks'} late` };
}

export default function Schedules() {
  const { user, ready } = useAuth();
  const [sp, setSp] = useSearchParams();
  const latestParams = useRef(sp);
  useEffect(() => { latestParams.current = sp; }, [sp]);
  const replaceParams = next => { latestParams.current = new URLSearchParams(next); setSp(latestParams.current, {replace:true}); };
  const projects = useProjectIndex(user);
  const [selected, setSelected] = useState(null);
  const [filtersOpen, setFiltersOpen] = useState(false);
  const [revision, setRevision] = useState(0);
  const projectFilter = sp.get('project') || '';
  const specific = useResource(s => api.project(projectFilter, s), [projectFilter,user?.id], {enabled:!!user && !!projectFilter});
  const view = sp.get('view') === 'timeline' ? 'timeline' : 'list';
  const group = sp.get('group') || 'contract';
  const query = sp.get('q') || '';
  const person = sp.get('person') || '';
  const week = sp.get('week') || '';
  const access = sp.get('access') || '';
  const set = (key, value) => {
    const next = new URLSearchParams(latestParams.current);
    if (value) next.set(key, value); else next.delete(key);
    replaceParams(next);
  };
  const all = [...(projects.data?.projects || [])];
  if (specific.data && !all.some(p => p.id === specific.data.id)) all.unshift(specific.data);
  const shown = projectFilter ? all.filter(p => String(p.id) === projectFilter) : all;
  if (ready && !user) return <Navigate to="/signin" replace state={{ from: '/schedules' }} />;
  return <Shell className="schedule-page">
    <div className="page-head">
      <div><h1 className="h1">schedules.</h1></div>
      <p className="sub">Work, people and access.</p>
    </div>
    <div className="workspace-tabs">
      <div className="seg" role="group" aria-label="Schedule view">
        <button className={`seg-btn ${view === 'list' ? 'on' : ''}`} aria-pressed={view === 'list'} onClick={() => set('view', 'list')}>table view</button>
        <button className={`seg-btn ${view === 'timeline' ? 'on' : ''}`} aria-pressed={view === 'timeline'} onClick={() => set('view', 'timeline')}>timeline</button>
      </div>
      <Link className="btn" to="/projects">manage projects </Link>
    </div>
    <button className="mobile-filter-toggle btn" aria-expanded={filtersOpen} aria-controls="schedule-filters" onClick={() => setFiltersOpen(v => !v)}>filters {filtersOpen ? '−' : '+'}</button>
    <div id="schedule-filters" className={`toolbar ${filtersOpen ? 'filters-open' : ''}`} role="group" aria-label="Schedule filters">
      <div className="field field-search"><label htmlFor="fq">find work</label><input id="fq" type="search" value={query} onChange={e => set('q', e.target.value)} /></div>
      <div className="field"><label htmlFor="fp">project</label><select id="fp" value={projectFilter} onChange={e => set('project', e.target.value)}><option value="">all projects</option>{all.map(p => <option key={p.id} value={p.id}>{p.name}</option>)}</select></div>
      <div className="field"><label htmlFor="fg">group by</label><select id="fg" value={group} onChange={e => set('group', e.target.value)}><option value="contract">contract</option><option value="person">coordinator</option><option value="project">project</option></select></div>
      <div className="field"><label htmlFor="fu">coordinator</label><select id="fu" value={person} onChange={e => set('person', e.target.value)}><option value="">anyone</option><option value={`me:${user?.id}`}>my work</option><option value="unassigned">unassigned</option></select></div>
      <div className="field"><label htmlFor="fw">week</label><input id="fw" type="number" min="1" max="520" step="1" value={week} onChange={e => set('week', e.target.value)} /></div>
      <div className="field"><label htmlFor="fa">access</label><select id="fa" value={access} onChange={e => set('access', e.target.value)}><option value="">all access</option><option value="standard">standard</option><option value="eclo">ECLO</option></select></div>
      {(query || person || week || access || projectFilter) && <button className="btn" onClick={() => replaceParams({ view, group })}>clear filters</button>}
    </div>
    {specific.error && <Failed error={specific.error} onRetry={specific.reload} what="load the project" />}
    <ProjectPages resource={projects} />
    {projects.loading && <Loading what="Loading projects" />}
    <Failed error={projects.error} onRetry={projects.reload} what="load the projects" />
    {!projects.loading && !projects.error && !shown.length && <Empty>No projects match this view. <Link to="/projects">Open projects</Link></Empty>}
    {shown.length > 0 && <p className="note">Conflict checks apply within each project.</p>}
    {shown.map((project,i) => <ProjectBlock initiallyOpen={i===0} key={project.id} project={project} view={view} group={group}
      filters={{ query, person, week, access, meId: user?.id }} revision={revision} onSelect={setSelected} />)}
    {selected && <ActivityDetail row={selected} onClose={() => setSelected(null)} onSaved={() => setRevision(r => r + 1)} />}
  </Shell>;
}

function ProjectBlock({ project, view, group, filters, revision, onSelect, initiallyOpen }) {
  const [open, setOpen] = useState(initiallyOpen);
  const [version, setVersion] = useState(null);
  const s = useProjectSchedule(project, version, open, revision);
  const state = planState(s.chosen);
  const rows = useMemo(() => filterRows(s.rows || [], filters), [s.rows, filters.query, filters.person, filters.week, filters.access, filters.meId]);
  const groups = useMemo(() => groupRows(rows, group), [rows, group]);
  const select = row => onSelect({ ...row, plan: s.chosen });
  return <section className="proj-block" aria-label={project.name}>
    <header className="proj-head">
      <button className="disclose" aria-expanded={open} onClick={() => setOpen(!open)}><span aria-hidden="true">{open ? '−' : '+'}</span>{project.name}</button>
      {s.chosen && <>
        <div className="plan-select"><label htmlFor={`plan-${project.id}`} className="sr-only">Plan for {project.name}</label><select id={`plan-${project.id}`} value={s.chosen.id} onChange={e => setVersion(e.target.value)}>{(s.versions.data?.versions || []).map(v => <option key={v.id} value={v.id}>scenario {v.scenario} / version {v.version_no}</option>)}</select></div>
        <span className={`tag ${state.failed ? 'tag-bad' : ''}`}>{state.failed ? 'failed validation' : state.label.toLowerCase()}</span>
      </>}
      <Link className="proj-link" to={`/projects/${project.id}`}>open project</Link>
    </header>
    {!open ? null : s.loading ? <Loading what={`Loading ${project.name}`} /> : s.error ? <Failed error={s.error} onRetry={s.reload} what="load this schedule" /> : !s.chosen ? <Empty>No plan yet. <Link to={`/projects/${project.id}`}>Generate a schedule</Link></Empty> : <>
      <div className="schedule-summary" aria-label="Filtered schedule summary">
        <p><strong>{new Set(rows.map(r => r.activityId)).size}</strong><span>activities</span></p>
        <p><strong>{rows.length}</strong><span>access slots</span></p>
        <p><strong>{new Set(rows.map(r => r.week)).size}</strong><span>active weeks</span></p>
        <p><strong>{rows.filter(r => r.eclo).length}</strong><span>ECLO slots</span></p>
      </div>
      {rows.length === 0 ? <Empty>No work matches these filters. Clear a filter to see more work.</Empty> : groups.map(([label, items]) => <div key={label}>
        <div className="group-label"><h3>{label}</h3><span>{items.length} access slots</span></div>
        {view === 'timeline' ? <Timeline rows={items} horizonStart={s.horizonStart} horizonWeeks={s.horizonWeeks} onSelect={select} /> : <WorkTable rows={items} horizonStart={s.horizonStart} onSelect={select} />}
      </div>)}
      <p className="legend"><span className="access-standard">standard access</span> · <span className="access-eclo">ECLO: early closure / late opening</span></p>
    </>}
  </section>;
}

function WorkTable({ rows, horizonStart, onSelect }) {
  const [sort, setSort] = useState({ field: 'week', direction: 'asc' });
  const [page, setPage] = useState(0);
  const sorted = useMemo(() => sortRows(rows, sort.field, sort.direction), [rows, sort]);
  const pages = Math.max(1, Math.ceil(sorted.length / LIST_PAGE_SIZE));
  const current = Math.min(page, pages - 1);
  const changeSort = field => { setSort(s => ({ field, direction: s.field === field && s.direction === 'asc' ? 'desc' : 'asc' })); setPage(0); };
  const heading = (field, label) => <th scope="col" aria-sort={sort.field === field ? (sort.direction === 'asc' ? 'ascending' : 'descending') : 'none'}><button className="sort-button" onClick={() => changeSort(field)}>{label} {sort.field === field ? (sort.direction === 'asc' ? '↓' : '↑') : ''}</button></th>;
  return <>
    <div className="schedule-reading-key">
      <p className="schedule-reading-note">Each row is one track-access slot. Timing is measured against the contract's planned completion week.</p>
      <p className="timing-legend" aria-label="Timing colours"><span className="timing-early"><i aria-hidden="true" />before target</span><span className="timing-on-time"><i aria-hidden="true" />target week</span><span className="timing-late"><i aria-hidden="true" />late</span></p>
    </div>
    <label className="mobile-sort">sort by
      <select value={`${sort.field}:${sort.direction}`} onChange={e => {const [field,direction]=e.target.value.split(':');setSort({field,direction});setPage(0);}}>
        <option value="week:asc">week / earliest first</option><option value="week:desc">week / latest first</option>
        <option value="activityId:asc">activity / ascending</option><option value="activityId:desc">activity / descending</option>
        <option value="priority:asc">priority / ascending</option><option value="priority:desc">priority / descending</option>
      </select>
    </label>
    <div className="scroll-x" role="region" aria-label="Scheduled work table" tabIndex={0}>
      <table className="data board-table"><caption className="sr-only">Scheduled work. Select an activity to inspect or assign it.</caption>
        <thead><tr>{heading('activityId', 'work')}{heading('week', 'when')}<th scope="col">where</th><th scope="col">owner</th><th scope="col">access</th></tr></thead>
        <tbody>{sorted.slice(current * LIST_PAGE_SIZE, (current + 1) * LIST_PAGE_SIZE).map(r => {
          const when = scheduleWeek(horizonStart, r.week);
          const where = readableLocation(r.location);
          const timing = timingStatus(r);
          const owner = r.coordinator?.coordinator_id ? {
            id: r.coordinator.coordinator_id,
            username: r.coordinator.coordinator,
            photo: r.coordinator.coordinator_photo,
          } : null;
          return <tr key={r.key} className={`timing-row-${timing.key}`}>
            <td className="work-col" data-label="work"><div className="work-identity">
              <button className="linklike work-name" onClick={() => onSelect(r)}>{r.activityId}</button>
              <span className="priority-chip">{r.priority ? `P${r.priority}` : 'no priority'}</span>
              <span className="work-sub">contract {r.contract || 'not supplied'}</span>
            </div></td>
            <td className="when-col" data-label="when"><span className="when-week">{when.week}</span><span className="work-sub">{when.date}</span><span className={`timing-chip timing-${timing.key}`}>{timing.label}</span></td>
            <td className="location-col" data-label="where" title={where.code}><span className="location-route">{where.route}</span>{where.context && <span className="work-sub">{where.context}</span>}<span className="location-code">{where.code}</span></td>
            <td className="owner-col" data-label="owner"><button className={`owner-button ${owner ? '' : 'owner-unassigned'}`} onClick={() => onSelect(r)} aria-label={`${owner ? 'Open' : 'Assign'} coordinator for ${r.activityId}`}>
              {owner ? <Avatar user={owner} size={36} /> : <span className="owner-placeholder" aria-hidden="true">+</span>}
              <span><span className="owner-name">{owner?.username || 'unassigned'}</span><span className="work-sub">{owner ? 'coordinator' : 'assign owner'}</span></span>
            </button></td>
            <td className="access-col" data-label="access"><span className={`access-chip ${r.eclo ? 'access-chip-eclo' : 'access-chip-standard'}`}><span className="access-dot" aria-hidden="true" />{r.eclo ? 'ECLO' : 'standard'}</span><span className="work-sub">slot {r.accessNight}</span></td>
          </tr>;
        })}</tbody>
      </table>
    </div>
    {pages > 1 && <Pagination current={current} pages={pages} onPage={setPage} />}
  </>;
}
function Pagination({ current, pages, onPage }) {
  return <nav className="pagination" aria-label="Schedule pages"><button className="btn" disabled={current === 0} onClick={() => onPage(current - 1)}>previous</button><span>page {current + 1} of {pages}</span><button className="btn" disabled={current === pages - 1} onClick={() => onPage(current + 1)}>next</button></nav>;
}
function Timeline({ rows, horizonStart, horizonWeeks, onSelect }) {
  const activities = useMemo(() => activityRows(rows), [rows]);
  const [page, setPage] = useState(0);
  const [start, setStart] = useState(1);
  const [windowSize, setWindowSize] = useState(() => window.innerWidth < 600 ? 3 : window.innerWidth < 1000 ? 6 : 12);
  useEffect(() => {
    const resize = () => setWindowSize(window.innerWidth < 600 ? 3 : window.innerWidth < 1000 ? 6 : 12);
    window.addEventListener('resize', resize);
    return () => window.removeEventListener('resize', resize);
  }, []);
  const last = Math.max(horizonWeeks || 1, ...rows.map(r => r.week));
  const first = Math.min(start, Math.max(1, last - windowSize + 1));
  const weeks = Array.from({ length: Math.min(windowSize, last - first + 1) }, (_, i) => first + i);
  const pages = Math.max(1, Math.ceil(activities.length / 40));
  const current = Math.min(page, pages - 1);
  return <div className="tl">
    <div className="pagination"><button className="btn" disabled={first === 1} onClick={() => setStart(Math.max(1, first - windowSize))}>earlier</button><span>weeks {first}–{weeks.at(-1)}</span><button className="btn" disabled={weeks.at(-1) >= last} onClick={() => setStart(first + windowSize)}>later</button></div>
    <div className="tl-scroll" role="region" tabIndex={0} aria-label="Weekly timeline">
      <table className="tl-table"><caption className="sr-only">One row per activity. Every scheduled access is retained when grouping by person.</caption>
        <thead><tr><th scope="col" className="tl-sticky">activity / coordinator</th>{weeks.map(w => <th scope="col" className="tl-wk" key={w} title={formatWeek(horizonStart, w)}>wk {w}</th>)}</tr></thead>
        <tbody>{activities.slice(current * 40, (current + 1) * 40).map(({ key, first: r, byWeek }) => <tr key={key}>
          <th scope="row" className="tl-sticky"><button className="linklike" onClick={() => onSelect(r)}>{r.activityId}</button><span className="work-sub">{r.coordinator?.coordinator || r.contract}</span></th>
          {weeks.map(w => <td key={w} className="tl-cell">{(byWeek.get(w) || []).map(slot => {
            const timing = timingStatus(slot);
            return <button key={slot.key} className={`blk timing-block-${timing.key} ${slot.eclo ? 'blk-eclo' : ''}`} aria-label={`${slot.activityId}, ${formatWeek(horizonStart, w)}, ${timing.label}, ${slot.eclo ? 'ECLO' : 'standard'} access ${slot.accessNight}`} title={timing.label} onClick={() => onSelect(slot)}>{slot.eclo ? 'E' : 'S'}</button>;
          })}</td>)}
        </tr>)}</tbody>
      </table>
    </div>
    {pages > 1 && <Pagination current={current} pages={pages} onPage={setPage} />}
  </div>;
}
