import { useCallback, useEffect, useRef, useState } from 'react';
import Shell from '../components/Shell.jsx';
import Avatar from '../components/Avatar.jsx';
import { useAuth } from '../state/auth.jsx';
import { api } from '../lib/api.js';
import { forgetPhotos } from '../lib/photos.js';
import { usePrefs } from '../state/prefs.jsx';
import { Navigate } from 'react-router-dom';

function Choice({ legend, name, value, options, onChange, note }) {
  return (
    <fieldset className="choice">
      <legend className="label">{legend}</legend>
      {options.map((o) => (
        <label key={o.value} className="radio">
          <input type="radio" name={name} value={o.value}
                 checked={value === o.value}
                 onChange={() => onChange(o.value)} />
          <span>{o.label}</span>
        </label>
      ))}
      {note ? <p className="label" style={{ marginTop: 'var(--gap-2)' }}>{note}</p> : null}
    </fieldset>
  );
}

function ProfilePhoto() {
  const { user } = useAuth();
  const fileRef = useRef(null);
  const [err, setErr] = useState('');
  const [success, setSuccess] = useState('');
  const [busy, setBusy] = useState(false);
  // Bumped after a write so the <img> re-requests instead of serving the
  // cached previous photo.
  const [rev, setRev] = useState(0);

  async function upload(file) {
    if (!file || busy) return;
    setBusy(true); setErr(''); setSuccess('');
    try {
      const fd = new FormData();
      fd.append('photo', file, file.name);
      await api.uploadPhoto(fd);
      // The old bytes are cached behind an object URL; drop them so the new
      // photo is fetched rather than the previous one being shown again.
      forgetPhotos(user?.id);
      setRev((r) => r + 1); setSuccess('Photo saved.');
    } catch (e) { setErr(e.message); }
    finally { setBusy(false); if (fileRef.current) fileRef.current.value = ''; }
  }

  async function remove() {
    if (busy) return;
    setBusy(true); setErr(''); setSuccess('');
    try { await api.removePhoto(); forgetPhotos(user?.id); setRev((r) => r + 1); setSuccess('Photo removed.'); }
    catch (e) { setErr(e.message); }
    finally { setBusy(false); }
  }

  return (
    <fieldset className="choice">
      <legend className="label">profile photo</legend>
      <div className="person-row">
        <Avatar key={rev} user={user ? { ...user, rev } : null} size={48} />
        <span>{user?.username}</span>
      </div>
      <p className="note">
        PNG or JPEG, up to 512 kB and 2048 by 2048 pixels. Initials are shown when there
        is no photo.
      </p>
      <input ref={fileRef} id="photo" aria-label="Profile photo" type="file" accept="image/png,image/jpeg"
             className="sr-only"
             onChange={(e) => upload(e.target.files?.[0])} />
      <div className="actions-row">
        <button type="button" className="btn btn-quiet" disabled={busy}
                onClick={() => fileRef.current?.click()}>
          {busy ? 'Working…' : 'Choose photo'}
        </button>
        <button type="button" className="btn btn-quiet" disabled={busy} onClick={remove}>
          Remove photo
        </button>
      </div>
      {success && <p className="note" role="status">{success}</p>}
      {err ? <p className="field-err" role="alert">{err}</p> : null}
    </fieldset>
  );
}

function AccountManagement({ currentUser }) {
  const [users, setUsers] = useState([]);
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [role, setRole] = useState('planner');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const [success, setSuccess] = useState('');

  const load = useCallback(async () => {
    try {
      const result = await api.users();
      setUsers(result.users || []);
    } catch (error) {
      setErr(error.message);
    }
  }, []);

  useEffect(() => { load(); }, [load]);

  async function createAccount(event) {
    event.preventDefault();
    if (busy) return;
    setErr(''); setSuccess('');
    if (!username.trim()) { setErr('Enter a username.'); return; }
    if (password.length < 12) { setErr('Use a password with at least 12 characters.'); return; }
    setBusy(true);
    try {
      await api.createUser(username.trim(), password, role);
      setSuccess(`${username.trim()} can now sign in as ${role}.`);
      setUsername(''); setPassword(''); setRole('planner');
      await load();
    } catch (error) {
      setErr(error.message);
    } finally {
      setBusy(false);
    }
  }

  async function setDisabled(account, disabled) {
    if (busy || account.id === currentUser.id) return;
    setBusy(true); setErr(''); setSuccess('');
    try {
      await api.setUserDisabled(account.id, disabled);
      setSuccess(`${account.username} has been ${disabled ? 'disabled' : 'enabled'}.`);
      await load();
    } catch (error) {
      setErr(error.message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="panel" aria-labelledby="accounts-heading">
      <h2 id="accounts-heading" className="h2">accounts.</h2>
      <p className="sub">Create one planner account for each tester. Planner accounts can create projects, upload data and generate plans.</p>
      <p className="note">To replace the debug administrator: create another administrator, sign out, sign in with the new account, then return here and disable the old account.</p>

      <form className="inline-form" onSubmit={createAccount} noValidate>
        <div className="field">
          <label htmlFor="account-username">username</label>
          <input id="account-username" value={username} onChange={(event) => setUsername(event.target.value)}
                 autoCapitalize="none" spellCheck="false" autoComplete="off" />
        </div>
        <div className="field">
          <label htmlFor="account-password">temporary password</label>
          <input id="account-password" type="password" value={password}
                 onChange={(event) => setPassword(event.target.value)}
                 autoComplete="new-password" />
        </div>
        <div className="field">
          <label htmlFor="account-role">role</label>
          <select id="account-role" value={role} onChange={(event) => setRole(event.target.value)}>
            <option value="planner">planner</option>
            <option value="viewer">viewer</option>
            <option value="approver">approver</option>
            <option value="administrator">administrator</option>
          </select>
        </div>
        <button className="btn" type="submit" disabled={busy}>
          {busy ? 'working…' : 'create account'}
        </button>
      </form>
      {success ? <p className="note" role="status">{success}</p> : null}
      {err ? <p className="field-err" role="alert">{err}</p> : null}

      <div className="scroll-x">
        <table className="data">
          <caption className="sr-only">Accounts in this installation</caption>
          <thead><tr><th scope="col">username</th><th scope="col">role</th><th scope="col">state</th><th scope="col">action</th></tr></thead>
          <tbody>{users.map((account) => (
            <tr key={account.id}>
              <td>{account.username}{account.id === currentUser.id ? ' (you)' : ''}</td>
              <td>{account.role}</td>
              <td>{account.disabled ? 'disabled' : 'active'}</td>
              <td>
                {account.id === currentUser.id
                  ? <span className="note">sign in as another administrator to disable this account</span>
                  : <button className="btn btn-quiet" type="button" disabled={busy}
                            onClick={() => setDisabled(account, !account.disabled)}>
                      {account.disabled ? 'enable' : 'disable'}
                    </button>}
              </td>
            </tr>
          ))}</tbody>
        </table>
      </div>
    </section>
  );
}

export default function Settings() {
  const { prefs, set } = usePrefs();
  const { user, ready } = useAuth();
  if (ready && !user) return <Navigate to="/signin" replace state={{ from: '/settings' }} />;
  return <Shell>
    <div className="page-head"><h1 className="h1">settings.</h1><p className="sub">Make yourself at home.</p></div>
    <div className="settings-grid">
      <Choice legend="appearance" name="theme" value={prefs.theme} onChange={v => set('theme', v)}
        options={[{value:'light',label:'paper'}, {value:'dark',label:'charcoal'}, {value:'system',label:'use device setting'}]} />
      <Choice legend="text size" name="textSize" value={prefs.textSize} onChange={v => set('textSize', v)}
        options={[{value:'normal',label:'standard'}, {value:'large',label:'large'}, {value:'larger',label:'extra large'}]} />
      <Choice legend="motion" name="motion" value={prefs.motion} onChange={v => set('motion', v)}
        options={[{value:'system',label:'use device setting'}, {value:'reduced',label:'reduce motion'}]} />
      <ProfilePhoto />
    </div>
    <p className="note">Appearance changes are saved on this device.</p>
    {user?.can?.manage_users ? <AccountManagement currentUser={user} /> : null}
  </Shell>;
}
