import { lazy, Suspense, useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Navigate, useParams, Link } from 'react-router-dom';
import Shell from '../components/Shell.jsx';
import { Loading, Failed } from '../components/state.jsx';
import { useAuth } from '../state/auth.jsx';
import { useResource } from '../lib/useResource.js';
import { api, clearCache } from '../lib/api.js';
import { INSTANCE_FILES, matchFiles } from '../lib/instanceFiles.js';

// The schedule view pulls in the timeline drawing; it is only fetched when an
// operator actually opens a plan.
const ScheduleView = lazy(() => import('../components/ScheduleView.jsx'));

const SCENARIOS = ['A', 'B', 'C'];
const STAGES = ['upload', 'check', 'generate', 'review'];

export default function Project() {
  const { pid } = useParams();
  const { user, ready } = useAuth();
  const [stage, setStage] = useState('upload');
  const [instanceId, setInstanceId] = useState(null);

  // Gated on a signed-in user. Without this the screen fires both requests
  // before the guard below redirects, producing two guaranteed 401s on every
  // cold open of a deep link.
  const signedIn = !!user;
  const instances = useResource((s) => api.instances(pid, s), [pid], { enabled: signedIn });
  const versions = useResource((s) => api.versions(pid, s), [pid], { enabled: signedIn });

  // Switching project must not leave the previous project's cached artefacts
  // reachable from this screen.
  useEffect(() => { setInstanceId(null); setStage('upload'); }, [pid]);

  if (ready && !user) return <Navigate to="/signin" replace state={{ from: `/workspace/${pid}` }} />;

  const list = instances.data?.instances || [];
  const current = instanceId || list[0]?.id || null;

  return (
    <Shell crumb={`project ${pid}`}>
      <h1 className="title">Plan the work</h1>

      <nav className="stages" aria-label="Stages">
        {STAGES.map((s, i) => (
          <button
            key={s}
            type="button"
            className={'stage' + (stage === s ? ' stage-on' : '')}
            aria-current={stage === s ? 'step' : undefined}
            onClick={() => setStage(s)}
          >
            <span className="beat-n">{String(i + 1).padStart(2, '0')}</span> {s}
          </button>
        ))}
      </nav>

      {stage === 'upload' ? (
        <Upload pid={pid} instances={instances} onLoaded={(id) => { setInstanceId(id); setStage('check'); }} />
      ) : null}

      {stage === 'check' ? (
        <Check instanceId={current} onNext={() => setStage('generate')} />
      ) : null}

      {stage === 'generate' ? (
        <Generate pid={pid} instanceId={current} onDone={() => { versions.reload(); setStage('review'); }} />
      ) : null}

      {stage === 'review' ? (
        <Review pid={pid} versions={versions} />
      ) : null}
    </Shell>
  );
}

/* ------------------------------------------------------------------ upload */

function Upload({ pid, instances, onLoaded }) {
  const [picked, setPicked] = useState(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const [dragging, setDragging] = useState(false);
  const inputRef = useRef(null);

  const report = picked ? matchFiles(picked) : null;

  async function upload() {
    if (!report || report.missing.length || busy) return;
    setBusy(true); setErr('');
    try {
      const fd = new FormData();
      for (const [name, file] of report.matched) fd.append(name, file, name);
      const r = await api.uploadInstance(pid, fd);
      clearCache(`instances:${pid}`);
      instances.reload();
      onLoaded(r.instance_id);
    } catch (e) {
      setErr(e.message);
    } finally { setBusy(false); }
  }

  async function loadSample() {
    setBusy(true); setErr('');
    try {
      const r = await api.loadDemo(pid);
      instances.reload();
      onLoaded(r.instance_id);
    } catch (e) { setErr(e.message); }
    finally { setBusy(false); }
  }

  return (
    <section>
      <p className="measure muted">
        Upload the eight PS1 files. Use the file picker or drop them here.
      </p>

      <div
        className={'drop' + (dragging ? ' drop-on' : '')}
        onDragOver={(e) => { e.preventDefault(); setDragging(true); }}
        onDragLeave={() => setDragging(false)}
        onDrop={(e) => { e.preventDefault(); setDragging(false); setPicked([...e.dataTransfer.files]); }}
      >
        {/* A file picker is mandatory on mobile, where there is nothing to drag. */}
        <input
          ref={inputRef}
          id="files"
          type="file"
          multiple
          accept=".csv,text/csv"
          className="sr-only"
          onChange={(e) => setPicked([...e.target.files])}
        />
        <button type="button" className="btn" onClick={() => inputRef.current.click()}>
          choose files
        </button>
        <p className="label" style={{ marginTop: 'var(--gap-3)' }}>or drop them in this area</p>
      </div>

      <table className="data" style={{ marginTop: 'var(--gap-5)' }}>
        <caption className="sr-only">The eight required files and whether each is present</caption>
        <thead><tr><th scope="col">file</th><th scope="col">state</th></tr></thead>
        <tbody>
          {INSTANCE_FILES.map((n) => {
            const f = report?.matched.get(n);
            return (
              <tr key={n}>
                <td className="id">{n}</td>
                <td className={f ? '' : 'muted'}>{f ? `ready / ${(f.size / 1024).toFixed(1)} kB` : 'not chosen'}</td>
              </tr>
            );
          })}
        </tbody>
      </table>

      {report?.extra.length ? (
        <p className="field-err" role="alert">
          These files are not part of the set and were ignored: {report.extra.join(', ')}.
        </p>
      ) : null}
      {err ? <p className="field-err" role="alert">{err}</p> : null}

      <div style={{ marginTop: 'var(--gap-5)', display: 'flex', gap: 'var(--gap-3)', flexWrap: 'wrap' }}>
        <button className="btn" onClick={upload} disabled={busy || !report || report.missing.length > 0}>
          {busy ? 'uploading…' : 'upload and check'}
        </button>
        <button className="btn btn-quiet" onClick={loadSample} disabled={busy}>
          use the sample data
        </button>
      </div>
      {report?.missing.length ? (
        <p className="label" style={{ marginTop: 'var(--gap-3)' }}>
          {report.missing.length} file(s) still needed.
        </p>
      ) : null}

      {instances.data?.instances?.length ? (
        <>
          <hr className="rule" style={{ marginTop: 'var(--gap-7)' }} />
          <h2 className="section-title" style={{ marginTop: 'var(--gap-5)' }}>Already uploaded</h2>
          <table className="data">
            <thead><tr><th scope="col">id</th><th scope="col">input hash</th><th scope="col">uploaded</th><th scope="col"></th></tr></thead>
            <tbody>
              {instances.data.instances.map((i) => (
                <tr key={i.id}>
                  <td className="num">{i.id}</td>
                  <td className="id">{(i.input_hash || '').slice(0, 12)}…</td>
                  <td className="muted">{(i.created_at || '').replace('T', ' ').replace('Z', '')}</td>
                  <td style={{ textAlign: 'right' }}>
                    <button className="linklike label" onClick={() => onLoaded(i.id)}>use this</button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      ) : null}
    </section>
  );
}

/* ------------------------------------------------------------------- check */

function Check({ instanceId, onNext }) {
  const r = useResource((s) => api.instanceDetail(instanceId, s), [instanceId],
                        { enabled: !!instanceId });
  if (!instanceId) return <p className="muted measure">Upload files first.</p>;
  if (r.loading) return <Loading what="Checking the input" />;
  if (r.error) return <Failed error={r.error} onRetry={r.reload} what="check the input" />;

  // The counts live under `summary`. The sibling keys (activities, contracts,
  // locations) are arrays of objects, not numbers - reading those directly is
  // what made this screen blank with React error #31.
  const d = r.data?.summary || {};
  const rows = [
    ['activities', d.activities], ['contracts', d.contracts],
    ['locations', d.locations], ['weeks', d.horizon_weeks],
    ['total access slots', d.total_accesses], ['exclusive pairs', d.exclusive_pairs],
  ].filter(([, v]) => v !== undefined);

  return (
    <section>
      <h2 className="section-title">The input reads correctly</h2>
      <p className="measure muted" style={{ marginTop: 'var(--gap-3)' }}>
        The service parsed every file and resolved the references between them.
      </p>
      <table className="data" style={{ marginTop: 'var(--gap-5)', maxWidth: '26rem' }}>
        <tbody>
          {rows.map(([k, v]) => (
            <tr key={k}><th scope="row">{k}</th><td className="num">{v}</td></tr>
          ))}
          {d.input_hash ? (
            <tr><th scope="row">input hash</th><td className="id">{d.input_hash.slice(0, 16)}…</td></tr>
          ) : null}
        </tbody>
      </table>
      <button className="btn" style={{ marginTop: 'var(--gap-5)' }} onClick={onNext}>
        generate plans
      </button>
    </section>
  );
}

/* ---------------------------------------------------------------- generate */

function Generate({ pid, instanceId, onDone }) {
  const [jobs, setJobs] = useState({});     // scenario -> job json
  const [err, setErr] = useState('');
  const [running, setRunning] = useState(false);
  const timers = useRef([]);

  useEffect(() => () => timers.current.forEach(clearTimeout), []);

  const start = useCallback(async () => {
    if (!instanceId || running) return;
    setRunning(true); setErr(''); setJobs({});
    try {
      // The three scenarios are independent, so they are submitted together and
      // the request layer holds the number of open connections down.
      const created = await Promise.all(
        SCENARIOS.map((sc) => api.createJob(pid, instanceId, sc, 120).then((j) => [sc, j]))
      );
      const next = {};
      for (const [sc, j] of created) next[sc] = j;
      setJobs(next);
      created.forEach(([sc, j]) => poll(sc, j.job_id));
    } catch (e) {
      setErr(e.message); setRunning(false);
    }
  }, [pid, instanceId, running]); // eslint-disable-line

  function poll(sc, id) {
    // Adaptive interval. The public instance solves all three scenarios in about
    // half a second, so a flat 700ms poll added up to 700ms of dead time after
    // the answer already existed. Start tight, then back off so a long solve
    // does not hammer the service.
    let wait = 120;
    const tick = async () => {
      try {
        const j = await api.job(id);
        setJobs((prev) => ({ ...prev, [sc]: j }));
        if (['queued', 'running'].includes(j.state)) {
          timers.current.push(setTimeout(tick, wait));
          wait = Math.min(Math.round(wait * 1.6), 2000);
        } else {
          setJobs((prev) => {
            const all = { ...prev, [sc]: j };
            const done = SCENARIOS.every((s) => all[s] && !['queued', 'running'].includes(all[s].state));
            if (done) { setRunning(false); onDone(); }
            return all;
          });
        }
      } catch (e) {
        setJobs((prev) => ({ ...prev, [sc]: { ...(prev[sc] || {}), state: 'failed', error: e.message } }));
        setRunning(false);
      }
    };
    tick();
  }

  if (!instanceId) return <p className="muted measure">Upload files first.</p>;

  return (
    <section>
      <h2 className="section-title">Generate A, B and C</h2>
      <p className="measure muted" style={{ marginTop: 'var(--gap-3)' }}>
        Each scenario is solved by the service. The state shown is the real job
        state; there is no progress estimate.
      </p>

      <table className="data" style={{ marginTop: 'var(--gap-5)' }}>
        <thead><tr><th scope="col">scenario</th><th scope="col">state</th><th scope="col">note</th></tr></thead>
        <tbody>
          {SCENARIOS.map((sc) => {
            const j = jobs[sc];
            return (
              <tr key={sc}>
                <th scope="row" className="id">{sc}</th>
                <td>{j ? j.state : 'not started'}</td>
                <td className="muted">
                  {j?.error ? j.error : j?.progress?.length ? j.progress[j.progress.length - 1] : ''}
                  {j && ['queued', 'running'].includes(j.state) ? (
                    <button className="linklike label" style={{ marginLeft: '1rem' }}
                            onClick={() => api.cancelJob(j.job_id).catch(() => {})}>cancel</button>
                  ) : null}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>

      {err ? <p className="field-err" role="alert">{err}</p> : null}

      <button className="btn" style={{ marginTop: 'var(--gap-5)' }} onClick={start} disabled={running}>
        {running ? 'solving…' : 'run all three'}
      </button>
    </section>
  );
}

/* ------------------------------------------------------------------ review */

function Review({ pid, versions }) {
  const [openVersion, setOpenVersion] = useState(null);
  if (versions.loading) return <Loading what="Loading plans" />;
  if (versions.error) return <Failed error={versions.error} onRetry={versions.reload} what="load the plans" />;

  const list = versions.data?.versions || [];
  if (!list.length) return <p className="muted measure">No plans yet. Generate them first.</p>;

  // Most recent version of each scenario, for the comparison.
  const latest = {};
  for (const v of list) {
    if (!latest[v.scenario] || v.version_no > latest[v.scenario].version_no) latest[v.scenario] = v;
  }

  return (
    <section>
      <h2 className="section-title">Compare the scenarios</h2>
      <div className="scroll-x" style={{ marginTop: 'var(--gap-5)' }}>
        <table className="data">
          <caption className="sr-only">Latest plan for each scenario</caption>
          <thead>
            <tr>
              <th scope="col">scenario</th><th scope="col">version</th><th scope="col">feasible</th>
              <th scope="col">hard violations</th><th scope="col">objective</th>
              <th scope="col">status</th><th scope="col">files</th>
            </tr>
          </thead>
          <tbody>
            {SCENARIOS.filter((s) => latest[s]).map((s) => {
              const v = latest[s];
              return (
                <tr key={s}>
                  <th scope="row" className="id">{s}</th>
                  <td className="num">{v.version_no}</td>
                  <td>{v.feasible ? 'yes' : 'no'}</td>
                  <td className="num">{v.violations}</td>
                  <td className="num">{v.objective}</td>
                  <td>{v.status}{v.is_fallback ? ' (fallback)' : ''}</td>
                  <td>
                    {['SCHEDULE_ACCESS.csv', 'SCHEDULE_OCCUPANCY.csv', 'RESULTS.csv'].map((f) => (
                      <a key={f} className="label file-link"
                         href={`/api/v1/versions/${v.id}/files/${f}`}>{f.replace('.csv', '')}</a>
                    ))}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      <p className="label" style={{ marginTop: 'var(--gap-4)' }}>
        Export links download the exact submission files.
      </p>

      <hr className="rule" style={{ marginTop: 'var(--gap-6)' }} />
      <h2 className="section-title" style={{ marginTop: 'var(--gap-5)' }}>Open a schedule</h2>
      <div style={{ display: 'flex', gap: 'var(--gap-3)', flexWrap: 'wrap', marginTop: 'var(--gap-4)' }}>
        {SCENARIOS.filter((s) => latest[s]).map((s) => (
          <button key={s} className="btn btn-quiet"
                  onClick={() => setOpenVersion(openVersion === latest[s].id ? null : latest[s].id)}>
            {openVersion === latest[s].id ? `hide ${s}` : `show ${s}`}
          </button>
        ))}
      </div>

      {openVersion ? (
        <Suspense fallback={<Loading what="Loading the schedule" />}>
          <ScheduleView versionId={openVersion} />
        </Suspense>
      ) : null}
    </section>
  );
}
