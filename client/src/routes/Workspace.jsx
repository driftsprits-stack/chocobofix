import { useState } from 'react';
import { Link, Navigate } from 'react-router-dom';
import Shell from '../components/Shell.jsx';
import { Loading, Failed, Empty } from '../components/state.jsx';
import { useAuth } from '../state/auth.jsx';
import { useResource } from '../lib/useResource.js';
import { api } from '../lib/api.js';

export default function Workspace() {
  const { user, ready } = useAuth();
  // Same gate as the project screen: do not fetch before the guard redirects.
  const { data, error, loading, reload } =
    useResource((s) => api.projects(s), [], { enabled: !!user });
  const [name, setName] = useState('');
  const [creating, setCreating] = useState(false);
  const [createErr, setCreateErr] = useState('');

  if (ready && !user) return <Navigate to="/signin" replace state={{ from: '/workspace' }} />;

  const projects = data?.projects || [];
  const canCreate = user?.can?.create_project;

  async function create(e) {
    e.preventDefault();
    if (creating || !name.trim()) return;
    setCreating(true); setCreateErr('');
    try { await api.createProject(name.trim()); setName(''); reload(); }
    catch (err) { setCreateErr(err.message); }
    finally { setCreating(false); }
  }

  return (
    <Shell>
      <h1 className="title">Projects</h1>
      <p className="measure muted" style={{ marginTop: 'var(--gap-3)' }}>
        A project holds one set of input files and the plans made from them.
      </p>

      {loading ? <Loading what="Loading projects" /> : null}
      <Failed error={error} onRetry={reload} what="load the projects" />

      {!loading && !error && projects.length === 0 ? (
        <Empty>No projects yet. Create one to upload input files.</Empty>
      ) : null}

      {projects.length > 0 ? (
        <table className="data" style={{ marginTop: 'var(--gap-5)' }}>
          <caption className="sr-only">Projects you can open</caption>
          <thead>
            <tr><th scope="col">name</th><th scope="col">created</th><th scope="col"><span className="sr-only">open</span></th></tr>
          </thead>
          <tbody>
            {projects.map((p) => (
              <tr key={p.id}>
                <td><Link to={`/workspace/${p.id}`}>{p.name}</Link></td>
                <td className="muted">{(p.created_at || '').replace('T', ' ').replace('Z', '')}</td>
                <td style={{ textAlign: 'right' }}>
                  <Link to={`/workspace/${p.id}`} className="label">open </Link>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      ) : null}

      {canCreate ? (
        <form onSubmit={create} style={{ marginTop: 'var(--gap-7)', maxWidth: '26rem' }}>
          <hr className="rule" />
          <h2 className="section-title" style={{ marginTop: 'var(--gap-5)' }}>New project</h2>
          <div className="field" style={{ marginTop: 'var(--gap-4)' }}>
            <label htmlFor="pn">project name</label>
            <input id="pn" value={name} onChange={(e) => setName(e.target.value)} />
          </div>
          {createErr ? <p className="field-err" role="alert">{createErr}</p> : null}
          <button className="btn" style={{ marginTop: 'var(--gap-4)' }} disabled={creating || !name.trim()}>
            {creating ? 'creating…' : 'create project'}
          </button>
        </form>
      ) : (
        <p className="label" style={{ marginTop: 'var(--gap-6)' }}>
          Your role cannot create projects.
        </p>
      )}
    </Shell>
  );
}
