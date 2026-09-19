import { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { useAuth } from '../state/auth.jsx';
import { api } from '../lib/api.js';

/**
 * The sample workflow.
 *
 * Honest about what it is: the sample dataset is loaded into a real project by
 * the same endpoint the normal workflow uses, and that endpoint requires an
 * account. There is no unauthenticated path to the solver, and inventing one
 * would mean either a second code path or an open endpoint. So this page says
 * so and offers the one-step route once signed in.
 */
export default function Sample() {
  const { user, ready } = useAuth();
  const nav = useNavigate();
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');

  async function open() {
    if (busy) return;
    setBusy(true); setErr('');
    try {
      const p = await api.createProject('Sample / PS1 public instance');
      await api.loadDemo(p.id);
      nav(`/workspace/${p.id}`);
    } catch (e) {
      setErr(e.message);
      setBusy(false);
    }
  }

  return (
    <main id="main" className="page" style={{ paddingBlock: 'var(--gap-7)' }}>
      <p className="label">sample</p>
      <h1 className="title" style={{ marginTop: 'var(--gap-3)' }}>
        The PS1 public instance
      </h1>
      <p className="measure" style={{ marginTop: 'var(--gap-4)' }}>
        The sample is the public PS1 dataset: 8 input files, 14 contracts. It
        loads into its own project, so it never touches other work.
      </p>

      <p className="measure muted" style={{ marginTop: 'var(--gap-4)' }}>
        This needs an account. The solver is not reachable without signing in,
        and this page does not pretend otherwise.
      </p>

      {err ? <p className="field-err" role="alert" style={{ marginTop: 'var(--gap-4)' }}>{err}</p> : null}

      <div style={{ marginTop: 'var(--gap-5)', display: 'flex', gap: 'var(--gap-3)', flexWrap: 'wrap' }}>
        {ready && user ? (
          <button className="btn" onClick={open} disabled={busy}>
            {busy ? 'setting up…' : 'load the sample project'}

          </button>
        ) : (
          <Link className="btn" to="/signin" state={{ from: '/sample' }}>
            sign in to continue
          </Link>
        )}
        <Link className="btn btn-quiet" to="/">back</Link>
      </div>
    </main>
  );
}
