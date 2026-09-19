import { useEffect, useRef, useState } from 'react';
import Avatar from './Avatar.jsx';
import { useAuth } from '../state/auth.jsx';
import { useResource } from '../lib/useResource.js';
import { api } from '../lib/api.js';
import { scenarioTagline, ECLO_FULL, formatWeek } from '../lib/scenarios.js';

/**
 * Detail for one scheduled access. Opens from a timeline block or a list row,
 * works with keyboard and touch, and never depends on hover.
 */
export default function ActivityDetail({ row, onClose, onSaved }) {
  const { user } = useAuth();
  const panel = useRef(null);
  const [assignErr, setAssignErr] = useState('');
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState(false);
  const inFlight = useRef(false);
  const [coordinator, setCoordinator] = useState(
    row.coordinator?.coordinator_id ? String(row.coordinator.coordinator_id) : ''
  );

  const users = useResource((s) => api.users(s), [], { enabled: !!user?.can?.manage_users });

  const closeRef = useRef(onClose);
  closeRef.current = onClose;
  useEffect(() => {
    const previous = document.activeElement;
    const oldOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    panel.current?.focus();
    const onKey = e => {
      if (e.key === 'Escape') closeRef.current();
      if (e.key !== 'Tab') return;
      const nodes = [...panel.current.querySelectorAll('button:not(:disabled), a[href], select:not(:disabled), input:not(:disabled), [tabindex="0"]')];
      const first = nodes[0], last = nodes.at(-1);
      if (!first) { e.preventDefault(); return; }
      if (e.shiftKey && (document.activeElement === first || document.activeElement === panel.current)) { e.preventDefault(); last.focus(); }
      else if (!e.shiftKey && (document.activeElement === last || document.activeElement === panel.current)) { e.preventDefault(); first.focus(); }
    };
    document.addEventListener('keydown', onKey);
    return () => { document.removeEventListener('keydown', onKey); document.body.style.overflow = oldOverflow; previous?.focus(); };
  }, []);

  const canAssign = !!user?.can?.run_solve;

  async function save(value) {
    if (inFlight.current) return;
    inFlight.current = true;
    const previous = coordinator;
    setCoordinator(value);
    setSaving(true); setAssignErr(''); setSaved(false);
    try {
      await api.assign(row.projectId, {
        instance_id: row.plan.instance_id,
        activity_id: row.activityId,
        coordinator_id: value,
      });
      setSaved(true);
      onSaved?.();
    } catch (e) {
      setCoordinator(previous); setAssignErr(e.message);
    } finally { setSaving(false); inFlight.current = false; }
  }

  const c = row.contractInfo;

  return (
    <div className="drawer-wrap" role="presentation" onClick={(e) => {
      if (e.target === e.currentTarget) onClose();
    }}>
      <aside
        className="drawer"
        role="dialog"
        aria-modal="true"
        aria-label={`Details for ${row.activityId}`}
        tabIndex={-1}
        ref={panel}
      >
        <div className="drawer-head">
          <h2 className="h2 mono">{row.activityId}</h2>
          <button type="button" className="btn btn-quiet" onClick={onClose}>Close</button>
        </div>

        <dl className="kvlist">
          <dt>Project</dt><dd>{row.projectName}</dd>
          <dt>Contract</dt>
          <dd>{row.contract}{c ? ` / ${c.description}` : ''}</dd>
          {c ? <><dt>Contract nature</dt><dd>{c.nature}</dd></> : null}
          <dt>Location</dt><dd className="mono">{row.location || 'not supplied'}</dd>
          {row.allLocations?.length > 1 ? (
            <><dt>Also occupies</dt>
              <dd className="mono small">{row.allLocations.filter(location => location !== row.location).join(', ')}</dd></>
          ) : null}
          <dt>Week</dt><dd>{formatWeek(row.horizonStart, row.week)}</dd>
          <dt>Access type</dt>
          <dd>{row.eclo ? `${ECLO_FULL} (ECLO)` : 'Standard access'}</dd>
          <dt>Access night index</dt>
          <dd>
            {row.accessNight}
            <span className="muted small">
              {' '} a local index for this contract and activity type in this week.
              It is not a weekday or a clock time.
            </span>
          </dd>
          <dt>Plan</dt>
          <dd>{scenarioTagline(row.plan.scenario)} · version {row.plan.version_no}</dd>
          <dt>Validation</dt>
          <dd>
            {row.plan.feasible && row.plan.violations === 0
              ? 'Passed the checks in this project.'
              : `Failed: ${row.plan.violations} hard violation(s).`}
            <span className="muted small">
              {' '}Checked within this project only. Passing checks does not mean the work is done.
            </span>
          </dd>
        </dl>

        <div className="assign">
          <h3 className="h3">Coordinator</h3>
          <p className="note">
            Who to contact about this activity. This is contact information: it does not
            confirm crew availability, qualification, or authority to enter the track.
          </p>
          <div className="person-row">
            <Avatar user={row.coordinator?.coordinator_id
              ? { id: row.coordinator.coordinator_id, username: row.coordinator.coordinator,
                  photo: row.coordinator.coordinator_photo }
              : null} size={32} />
            <span>{(users.data?.users || []).find(u => String(u.id) === coordinator)?.username || (coordinator ? row.coordinator?.coordinator : 'Unassigned')}</span>
          </div>

          {canAssign ? (
            <div className="field">
              <label htmlFor="coord">Assign coordinator</label>
              <select id="coord" value={coordinator} disabled={saving}
                      onChange={(e) => save(e.target.value)}>
                <option value="">Unassigned</option>
                {(users.data?.users || (user ? [user] : [])).filter((u) => !u.disabled).map((u) => (
                  <option key={u.id} value={String(u.id)}>{u.username}</option>
                ))}
              </select>
              {assignErr ? <p className="field-err" role="alert">{assignErr}</p> : null}
              {saving ? <p className="note" role="status">Saving…</p> : saved ? <p className="note" role="status">Coordinator saved.</p> : null}
            </div>
          ) : (
            <p className="note">Your role cannot change the coordinator.</p>
          )}
        </div>
      </aside>
    </div>
  );
}
