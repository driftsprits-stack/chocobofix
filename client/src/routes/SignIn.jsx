import { useRef, useState } from 'react';
import { useNavigate, Link, useLocation } from 'react-router-dom';
import { useAuth } from '../state/auth.jsx';

export default function SignIn() {
  const { signIn, needsBootstrap, ready } = useAuth();
  const nav = useNavigate();
  const loc = useLocation();
  const [errs, setErrs] = useState({});
  const [accepted, setAccepted] = useState(loc.state?.termsAccepted === true);
  const [formError, setFormError] = useState('');
  const [busy, setBusy] = useState(false);
  // Rotation pauses while the operator is actually filling the form in.
  const [interacting, setInteracting] = useState(false);
  const inFlight = useRef(false);
  const userRef = useRef(null);
  const passRef = useRef(null);

  const first = needsBootstrap;

  async function submit(e) {
    e.preventDefault();
    // A second Enter can arrive before React re-renders the disabled button.
    if (inFlight.current) return;

    if (!accepted) { setFormError('Read and accept the terms to continue.'); return; }
    const username = userRef.current.value.trim();
    const password = passRef.current.value;
    const next = {};
    if (!username) next.username = 'Enter a username.';
    if (!password) next.password = 'Enter a password.';
    if (first && password && password.length < 12)
      next.password = 'Use at least 12 characters.';
    setErrs(next);
    setFormError('');
    if (Object.keys(next).length) {
      (next.username ? userRef : passRef).current.focus();
      return;
    }

    inFlight.current = true;
    setBusy(true);
    try {
      await signIn(username, password, { bootstrap: first });
      nav(loc.state?.from || '/workspace', { replace: true });
    } catch (err) {
      // The username stays in the field. The password is cleared, because it is
      // the part that was probably wrong.
      if (err.status === 401) setErrs({ password: err.message });
      else if (err.status === 400) setErrs({ password: err.message });
      else setFormError(err.message);
      passRef.current.value = '';
      passRef.current.focus();
    } finally {
      inFlight.current = false;
      setBusy(false);
    }
  }

  return (
    <main id="main" className="auth-page page">
      <header className="auth-top"><Link className="brand" to="/">chocobofix</Link><Link to="/terms">terms</Link></header>
      <div className="auth-inner">
        <section className="auth-story"><h2 className="display">make room<br />for the work.</h2><p>Clear plans for the hours between the last train and the first.</p></section>
        <section className="auth-form-area">
        <h1 className="h2">{first ? 'Set up the first administrator' : 'Sign in'}</h1>
        <p className="sub">
          {first
            ? 'No account exists yet. The details you enter here create the administrator account for this installation.'
            : 'An administrator creates accounts. There is no self-registration.'}
        </p>

      <form onSubmit={submit} noValidate
            onFocusCapture={() => setInteracting(true)}
            onBlurCapture={() => setInteracting(false)}
            className="auth-form">
        <div className="field">
          <label htmlFor="u">username</label>
          <input id="u" ref={userRef} type="text" autoComplete="username"
                 autoCapitalize="none" spellCheck="false"
                 aria-invalid={errs.username ? 'true' : 'false'}
                 aria-describedby={errs.username ? 'uErr' : undefined} />
          {errs.username ? <p className="field-err" id="uErr" role="alert">{errs.username}</p> : null}
        </div>

        <div className="field">
          <label htmlFor="p">password</label>
          <input id="p" ref={passRef} type="password"
                 autoComplete={first ? 'new-password' : 'current-password'}
                 aria-invalid={errs.password ? 'true' : 'false'}
                 aria-describedby={errs.password ? 'pErr' : undefined} />
          {errs.password ? <p className="field-err" id="pErr" role="alert">{errs.password}</p> : null}
        </div>

        <label className="radio"><input type="checkbox" checked={accepted} onChange={e => setAccepted(e.target.checked)} /><span>I agree to the <Link to="/terms">terms</Link> and have read the <Link to="/privacy">privacy notice</Link>.</span></label>
        {formError ? <p className="field-err" role="alert">{formError}</p> : null}

        <div className="actions-row">
          <button className="btn" type="submit" disabled={busy || !ready || !accepted}>
            {busy ? 'working…' : first ? 'create administrator' : 'sign in'}

          </button>
          <Link className="btn btn-quiet" to="/">back</Link>
        </div>
        </form>
        </section>
      </div>
    </main>
  );
}
