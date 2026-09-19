import { useRef, useState } from 'react';
import { Link, Navigate, useNavigate } from 'react-router-dom';
import { useProjectIndex } from '../lib/useProjectIndex.js';
import ProjectPages from '../components/ProjectPages.jsx';
import Shell from '../components/Shell.jsx';
import { Loading, Failed, Empty } from '../components/state.jsx';
import { useAuth } from '../state/auth.jsx';
import { useResource } from '../lib/useResource.js';
import { api, clearCache } from '../lib/api.js';

export default function Projects() {
  const { user, ready } = useAuth();
  const nav = useNavigate();
  const index = useProjectIndex(user);
  const { data, error, loading, reload } = index;

  const [name, setName] = useState('');
  const [creating, setCreating] = useState(false);
  const [createErr, setCreateErr] = useState('');
  // A ref, not state: it blocks a second submit in the same tick, before React
  // has re-rendered the disabled button.
  const inFlight = useRef(false);

  if (ready && !user) return <Navigate to="/signin" replace state={{ from: '/projects' }} />;

  const projects = data?.projects || [];
  const canCreate = !!user?.can?.create_project;

  async function create(e) {
    e.preventDefault();
    const trimmed = name.trim();
    if (inFlight.current || !trimmed) return;

    inFlight.current = true;
    setCreating(true);
    setCreateErr('');
    try {
      // Wait for the server-created id, then go straight to that project's
      // upload step. `replace` keeps the creation form out of history, so Back
      // and a refresh cannot submit it again.
      const p = await api.createProject(trimmed);
      clearCache('projects');
      setName('');
      nav(`/projects/${p.id}/upload`, { replace: true });
    } catch (err) {
      // The typed name is kept so the operator can correct and retry.
      setCreateErr(err.message);
      setCreating(false);
      inFlight.current = false;
    }
  }

  return (
    <Shell>
      <div className="page-head">
        <div><h1 className="h1">projects.</h1></div>
        <p className="sub">Your railway access plans.</p>
      </div>

      {loading ? <Loading what="Loading projects" /> : null}
      <Failed error={error} onRetry={reload} what="load the projects" />

      {!loading && !error && projects.length === 0 ? (
        <Empty>No projects yet. Create one below, then upload the eight CSV files.</Empty>
      ) : null}

      {projects.length > 0 ? (
        <div className="scroll-x">
          <table className="data responsive-data">
            <caption className="sr-only">Projects you can open</caption>
            <thead>
              <tr>
                <th scope="col">Project</th>
                <th scope="col">Owner</th>
                <th scope="col">Created</th>
                <th scope="col"><span className="sr-only">Actions</span></th>
              </tr>
            </thead>
            <tbody>
              {projects.map((p) => (
                <tr key={p.id}>
                  <td><Link to={`/projects/${p.id}`} className="strong-link">{p.name}</Link></td>
                  <td data-label="owner" className="muted">{p.owner}</td>
                  <td data-label="created" className="muted">{(p.created_at || '').replace('T', ' ').replace('Z', '')}</td>
                  <td className="row-actions">
                    <Link to={`/projects/${p.id}/upload`}>Upload files</Link>
                    <Link to={`/schedules?project=${p.id}`}>View schedule</Link>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : null}

      <ProjectPages resource={index} />
      {canCreate ? (
        <section className="panel new-project">
          <div><h2 className="h2">a new plan.</h2><p className="note">Name the project, then add your files.</p></div>
          <form onSubmit={create} className="inline-form">
            <div className="field">
              <label htmlFor="pn">Project name</label>
              <input
                id="pn" type="text" maxLength={120} required
                value={name}
                onChange={(e) => setName(e.target.value)}
                aria-invalid={createErr ? 'true' : 'false'}
                aria-describedby={createErr ? 'pnErr' : undefined}
                disabled={creating}
              />
              {createErr ? <p className="field-err" id="pnErr" role="alert">{createErr}</p> : null}
            </div>
            <button className="btn" disabled={creating || !name.trim()}>
              {creating ? 'Creating…' : 'create project'}
            </button>
          </form>
        </section>
      ) : (
        <p className="note" style={{ marginTop: '2rem' }}>Your role cannot create projects.</p>
      )}
    </Shell>
  );
}
