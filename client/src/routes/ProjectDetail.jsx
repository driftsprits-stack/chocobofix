import { useCallback, useEffect, useRef, useState } from 'react';
import { Link, Navigate, useNavigate, useParams, useSearchParams } from 'react-router-dom';
import Shell from '../components/Shell.jsx';
import { Loading, Failed, Empty } from '../components/state.jsx';
import { useAuth } from '../state/auth.jsx';
import { useResource } from '../lib/useResource.js';
import { api } from '../lib/api.js';
import { SCENARIOS, scenarioTagline, scenarioExplain } from '../lib/scenarios.js';

/** Setup and source data for one project. Deliberately no schedule grid: the
 *  schedule lives on /schedules, where every project's work is together. */
export default function ProjectDetail() {
  const { pid } = useParams();
  const { user, ready } = useAuth();
  const nav = useNavigate();
  const [sp] = useSearchParams();

  const projects = useResource((s) => api.project(pid, s), [pid], { enabled: !!user });
  const instances = useResource((s) => api.instances(pid, s), [pid], { enabled: !!user });
  const versions = useResource((s) => api.versions(pid, s), [pid], { enabled: !!user });

  if (ready && !user) return <Navigate to="/signin" replace state={{ from: `/projects/${pid}` }} />;

  const project = projects.data;
  const list = instances.data?.instances || [];
  const wanted = sp.get('instance');
  const current = list.find((i) => String(i.id) === String(wanted)) || list[0] || null;

  return (
    <Shell crumb={project ? project.name : `Project ${pid}`}>
      <div className="page-head">
        <h1 className="h1">{project ? project.name : `Project ${pid}`}</h1>
        <p className="sub">Source data and plan generation for this project.</p>
      </div>

      <div className="actions-row">
        <Link className="btn" to={`/schedules?project=${pid}`}>View schedule</Link>
        <Link className="btn btn-quiet" to={`/projects/${pid}/upload`}>Upload files</Link>
      </div>

      <Failed error={projects.error} onRetry={projects.reload} what="load the project" />
      {instances.loading ? <Loading what="Loading source data" /> : null}
      <Failed error={instances.error} onRetry={instances.reload} what="load the source data" />

      {!instances.loading && !instances.error && !current ? (
        <Empty>
          No files uploaded yet. <Link to={`/projects/${pid}/upload`}>Upload the eight CSV files</Link> to continue.
        </Empty>
      ) : null}

      {current ? <SourceData instanceId={current.id} /> : null}
      {current ? (
        <Generate
          pid={pid}
          instanceId={current.id}
          canSolve={!!user?.can?.run_solve}
          onDone={() => { versions.reload(); }}
          onOpenSchedule={() => nav(`/schedules?project=${pid}`)}
        />
      ) : null}
    </Shell>
  );
}

function SourceData({ instanceId }) {
  const r = useResource((s) => api.instanceDetail(instanceId, s), [instanceId]);
  if (r.loading) return <Loading what="Checking the files" />;
  if (r.error) return <Failed error={r.error} onRetry={r.reload} what="check the files" />;

  const d = r.data?.summary || {};
  const rows = [
    ['Activities', d.activities], ['Contracts', d.contracts],
    ['Locations', d.locations], ['Weeks in horizon', d.horizon_weeks],
    ['Access slots required', d.total_accesses],
  ].filter(([, v]) => v !== undefined);

  return (
    <section className="panel">
      <h2 className="h2">Source data</h2>
      <p className="note">The files read correctly and their references resolve.</p>
      <table className="data kv">
        <tbody>
          {rows.map(([k, v]) => (
            <tr key={k}><th scope="row">{k}</th><td className="num">{v}</td></tr>
          ))}
          {d.horizon_start ? (
            <tr><th scope="row">Week 1 begins</th><td>{d.horizon_start}</td></tr>
          ) : null}
          {d.input_hash ? (
            <tr><th scope="row">Input hash</th><td className="mono">{d.input_hash.slice(0, 16)}…</td></tr>
          ) : null}
        </tbody>
      </table>
    </section>
  );
}

function Generate({ pid, instanceId, canSolve, onDone, onOpenSchedule }) {
  const [jobs, setJobs] = useState({});
  const [err, setErr] = useState('');
  const [running, setRunning] = useState(false);
  const [finished, setFinished] = useState(false);
  const timers = useRef([]);
  useEffect(() => () => timers.current.forEach(clearTimeout), []);

  const start = useCallback(async () => {
    if (!instanceId || running) return;
    setRunning(true); setErr(''); setJobs({}); setFinished(false);
    try {
      const created = await Promise.all(
        SCENARIOS.map((sc) => api.createJob(pid, instanceId, sc, 120).then((j) => [sc, j]))
      );
      const next = {};
      for (const [sc, j] of created) next[sc] = j;
      setJobs(next);
      created.forEach(([sc, j]) => poll(sc, j.job_id));
    } catch (e) { setErr(e.message); setRunning(false); }
  }, [pid, instanceId, running]); // eslint-disable-line

  function poll(sc, id) {
    let wait = 120;
    const tick = async () => {
      try {
        const j = await api.job(id);
        setJobs((prev) => {
          const all = { ...prev, [sc]: j };
          if (!['queued', 'running'].includes(j.state)) {
            const done = SCENARIOS.every((s) => all[s] && !['queued', 'running'].includes(all[s].state));
            if (done) { setRunning(false); setFinished(true); onDone(); }
          }
          return all;
        });
        if (['queued', 'running'].includes(j.state)) {
          timers.current.push(setTimeout(tick, wait));
          wait = Math.min(Math.round(wait * 1.6), 2000);
        }
      } catch (e) {
        setJobs((prev) => ({ ...prev, [sc]: { ...(prev[sc] || {}), state: 'failed', error: e.message } }));
        setRunning(false);
      }
    };
    tick();
  }

  if (!canSolve) {
    return <p className="note" style={{ marginTop: '2rem' }}>Your role cannot generate plans.</p>;
  }

  return (
    <section className="panel">
      <h2 className="h2">Generate plans</h2>
      <p className="note">
        All three policies are always produced. They answer different questions, so their
        scores are not comparable to each other.
      </p>

      <ul className="scenario-legend">
        {SCENARIOS.map((sc) => (
          <li key={sc}>
            <span className="scenario-tag">{scenarioTagline(sc)}</span>
            <span className="muted"> {scenarioExplain(sc)}</span>
          </li>
        ))}
      </ul>

      <div className="scroll-x">
        <table className="data">
          <thead><tr><th scope="col">Policy</th><th scope="col">State</th><th scope="col">Note</th></tr></thead>
          <tbody>
            {SCENARIOS.map((sc) => {
              const j = jobs[sc];
              return (
                <tr key={sc}>
                  <th scope="row">{scenarioTagline(sc)}</th>
                  <td>{j ? j.state : 'Not started'}</td>
                  <td className="muted">
                    {j?.error ? j.error : j?.progress?.length ? j.progress[j.progress.length - 1] : ''}
                    {j && ['queued', 'running'].includes(j.state) ? (
                      <button className="linklike" style={{ marginLeft: '1rem' }}
                              onClick={() => api.cancelJob(j.job_id).catch(() => {})}>Cancel</button>
                    ) : null}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      {err ? <p className="field-err" role="alert">{err}</p> : null}

      <div className="actions-row">
        <button className="btn" onClick={start} disabled={running}>
          {running ? 'Solving…' : 'Generate plans'}
        </button>
        {finished ? (
          <button className="btn btn-quiet" onClick={onOpenSchedule}>View schedule</button>
        ) : null}
      </div>
    </section>
  );
}
