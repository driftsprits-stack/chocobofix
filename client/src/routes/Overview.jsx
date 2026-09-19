import { useState } from 'react';
import { Link, Navigate } from 'react-router-dom';
import { useProjectIndex } from '../lib/useProjectIndex.js';
import ProjectPages from '../components/ProjectPages.jsx';
import Shell from '../components/Shell.jsx';
import { Loading, Failed, Empty } from '../components/state.jsx';
import { useAuth } from '../state/auth.jsx';
import { useResource } from '../lib/useResource.js';
import { useProjectSchedule } from '../lib/useProjectSchedule.js';
import { api } from '../lib/api.js';
import { formatWeek } from '../lib/scenarios.js';

export default function Overview() {
  const { user, ready } = useAuth();
  const projects = useProjectIndex(user);
  const [selected, setSelected] = useState('');
  const all = projects.data?.projects || [];
  const project = all.find(p => String(p.id) === selected) || all[0];
  if (ready && !user) return <Navigate to="/signin" replace state={{from:'/overview'}} />;
  return <Shell>
    <div className="page-head"><h1 className="h1">hello,<br />{user?.username || 'there'}.</h1><p className="sub">{new Date().toLocaleDateString('en', {weekday:'long', day:'numeric', month:'long', year:'numeric'})}</p></div>
    {projects.loading && <Loading what="Loading your projects" />}
    <Failed error={projects.error} onRetry={projects.reload} what="load your projects" />
    {!projects.loading && !projects.error && !all.length && <Empty>Your workspace is ready. <Link to="/projects">Create a project</Link> or <Link to="/sample">try the public sample</Link>.</Empty>}
    <ProjectPages resource={projects} />
    {project && <><div className="field overview-plan"><label htmlFor="overview-project">project</label><select id="overview-project" value={project.id} onChange={e => setSelected(e.target.value)}>{all.map(p => <option key={p.id} value={p.id}>{p.name}</option>)}</select></div><PlanOverview key={project.id} project={project} /></>}
  </Shell>;
}
function PlanOverview({project}) {
  const s = useProjectSchedule(project, null, true);
  const now = new Date();
  const today = Date.UTC(now.getFullYear(), now.getMonth(), now.getDate());
  const start = s.horizonStart ? Date.parse(`${s.horizonStart}T00:00:00Z`) : NaN;
  const currentWeek = Number.isFinite(start) ? Math.floor((today-start)/604800000)+1 : null;
  const rows = s.rows || [];
  const current = rows.filter(r => r.week === currentWeek);
  const counts = new Map();
  for (const row of rows) counts.set(row.week, (counts.get(row.week) || 0) + 1);
  const weeks = [...counts].sort((a,b) => a[0]-b[0]);
  const max = Math.max(1, ...weeks.map(([,n]) => n));
  if (s.loading) return <Loading what="Loading your plan" />;
  if (s.error) return <Failed error={s.error} onRetry={s.reload} what="load this plan" />;
  if (!s.chosen) return <Empty>No schedule yet. <Link to={`/projects/${project.id}`}>Generate a plan</Link>.</Empty>;
  return <>
    <div className="overview-head"><h2 className="h2">this week.</h2><Link className="btn" to={`/schedules?project=${project.id}`}>open schedule</Link></div>
    <p className="note">Scenario {s.chosen.scenario} / version {s.chosen.version_no}. Plans specify access weeks, not daily appointments.</p>
    {current.length ? <div className="schedule-summary"><p><strong>{new Set(current.map(r=>r.activityId)).size}</strong><span>activities this week</span></p><p><strong>{current.length}</strong><span>access slots</span></p><Link className="btn" to={`/schedules?project=${project.id}&week=${currentWeek}`}>view this week</Link></div> : <p className="empty-state">{currentWeek === null ? 'This plan has no calendar dates.' : 'No access scheduled for this week.'}</p>}
    {weeks.length > 0 && <section aria-labelledby="workload-title">
      <div className="overview-head"><h2 id="workload-title" className="h2">access ahead.</h2><span className="note">planned slots / week</span></div>
      <svg className="overview-chart" viewBox="0 0 900 220" role="img" aria-labelledby="chart-title chart-desc" preserveAspectRatio="none"><title id="chart-title">Access slots by active week</title><desc id="chart-desc">{weeks.map(([w,n])=>`Week ${w}: ${n} slots`).join('. ')}</desc>{weeks.map(([w,n],i)=><rect key={w} x={i*900/weeks.length+2} y={200-n/max*185} width={Math.max(1,900/weeks.length-4)} height={n/max*185} fill="currentColor" />)}<line x1="0" x2="900" y1="201" y2="201" stroke="var(--rule)" /></svg>
      <details><summary>view weekly totals</summary><div className="weeks">{weeks.map(([w,n])=><Link key={w} className="btn" to={`/schedules?project=${project.id}&week=${w}`}>{formatWeek(s.horizonStart,w)} / {n} slots</Link>)}</div></details>
    </section>}
  </>;
}
