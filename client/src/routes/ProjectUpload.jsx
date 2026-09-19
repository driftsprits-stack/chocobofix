import { useRef, useState } from 'react';
import { Link, Navigate, useNavigate, useParams } from 'react-router-dom';
import Shell from '../components/Shell.jsx';
import { Loading, Failed } from '../components/state.jsx';
import { useAuth } from '../state/auth.jsx';
import { useResource } from '../lib/useResource.js';
import { api, clearCache } from '../lib/api.js';
import { INSTANCE_FILES, matchFiles } from '../lib/instanceFiles.js';

/** Step 1 of the journey: upload the eight files. This is where a newly created
 *  project lands, with no overview in between. */
export default function ProjectUpload() {
  const { pid } = useParams();
  const { user, ready } = useAuth();
  const nav = useNavigate();

  const projects = useResource((s) => api.project(pid, s), [pid], { enabled: !!user });
  const instances = useResource((s) => api.instances(pid, s), [pid], { enabled: !!user });

  const [picked, setPicked] = useState(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const [dragging, setDragging] = useState(false);
  const inputRef = useRef(null);
  const inFlight = useRef(false);

  if (ready && !user) return <Navigate to="/signin" replace state={{ from: `/projects/${pid}/upload` }} />;

  const project = projects.data;
  const report = picked ? matchFiles(picked) : null;
  const uploaded = instances.data?.instances || [];

  async function upload() {
    if (!report || report.missing.length || inFlight.current) return;
    if ([...report.matched.values()].reduce((n,file)=>n+file.size,0) > 31*1024*1024) { setErr('Use CSV files with a combined size below 31 MB.'); return; }
    inFlight.current = true; setBusy(true); setErr('');
    try {
      const fd = new FormData();
      for (const [name, file] of report.matched) fd.append(name, file, name);
      const r = await api.uploadInstance(pid, fd);
      clearCache(`instances:${pid}`);
      instances.reload();
      nav(`/projects/${pid}?instance=${r.instance_id}`);
    } catch (e) {
      setErr(e.message);
    } finally { inFlight.current = false; setBusy(false); }
  }

  async function loadSample() {
    if (inFlight.current) return;
    inFlight.current = true; setBusy(true); setErr('');
    try {
      const r = await api.loadDemo(pid);
      clearCache(`instances:${pid}`);
      instances.reload();
      nav(`/projects/${pid}?instance=${r.instance_id}`);
    } catch (e) { setErr(e.message); }
    finally { inFlight.current = false; setBusy(false); }
  }

  return (
    <Shell crumb={project ? project.name : `Project ${pid}`}>
      <div className="page-head">
        <h1 className="h1">source files.</h1>
        <p className="sub">
          {project ? project.name : `Project ${pid}`} / the files describe the network, access
          supply, contracts and activities.
        </p>
      </div>

      <div
        className={'drop' + (dragging ? ' drop-on' : '')}
        onDragOver={(e) => { e.preventDefault(); setDragging(true); }}
        onDragLeave={() => setDragging(false)}
        onDrop={(e) => { e.preventDefault(); setDragging(false); setPicked([...e.dataTransfer.files]); }}
      >
        {/* The picker is mandatory: there is nothing to drag on a phone. */}
        <input ref={inputRef} id="files" aria-label="Source CSV files" type="file" multiple accept=".csv,text/csv"
               className="sr-only"
               onChange={(e) => setPicked([...e.target.files])} />
        <button type="button" className="btn" onClick={() => inputRef.current.click()}>
          Choose files
        </button>
        <p className="note">or drop the eight files into this area</p>
      </div>

      <table className="data checklist">
        <caption className="sr-only">The eight expected files and the state of each</caption>
        <thead><tr><th scope="col">File</th><th scope="col">State</th></tr></thead>
        <tbody>
          {INSTANCE_FILES.map((n) => {
            const f = report?.matched.get(n);
            return (
              <tr key={n} className={report && !f ? 'row-missing' : ''}>
                <td className="mono">{n}</td>
                <td className={f ? '' : 'muted'}>
                  {f
                    ? `Ready / ${(f.size / 1024).toFixed(1)} kB`
                    : report ? 'Missing. Add this file.' : 'Not chosen yet'}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>

      {report?.extra.length ? (
        <p className="field-err" role="alert">
          Not part of the set, so ignored: {report.extra.join(', ')}. Remove them or rename them.
        </p>
      ) : null}
      {err ? <p className="field-err" role="alert">{err}</p> : null}

      <div className="actions-row">
        <button className="btn" onClick={upload}
                disabled={busy || !report || report.missing.length > 0}>
          {busy ? 'Uploading…' : 'Check files'}
        </button>
        <button className="btn btn-quiet" onClick={loadSample} disabled={busy}>
          Use the sample data
        </button>
        <Link className="btn btn-quiet" to="/projects">Back to projects</Link>
      </div>
      {report?.missing.length ? (
        <p className="note">{report.missing.length} of 8 files still needed.</p>
      ) : null}

      {instances.loading ? <Loading what="Loading uploads" /> : null}
      <Failed error={instances.error} onRetry={instances.reload} what="load earlier uploads" />

      {uploaded.length ? (
        <section className="panel" style={{ marginTop: '2rem' }}>
          <h2 className="h2">Already uploaded</h2>
          <div className="scroll-x">
            <table className="data">
              <thead><tr><th scope="col">Instance</th><th scope="col">Input hash</th><th scope="col">Uploaded</th><th scope="col"></th></tr></thead>
              <tbody>
                {uploaded.map((i) => (
                  <tr key={i.id}>
                    <td className="num">{i.id}</td>
                    <td className="mono">{(i.input_hash || '').slice(0, 12)}…</td>
                    <td className="muted">{(i.created_at || '').replace('T', ' ').replace('Z', '')}</td>
                    <td className="row-actions">
                      <Link to={`/projects/${pid}?instance=${i.id}`}>Open</Link>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>
      ) : null}
    </Shell>
  );
}
