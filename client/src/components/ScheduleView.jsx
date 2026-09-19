import { useMemo, useState } from 'react';
import { useResource } from '../lib/useResource.js';
import { api } from '../lib/api.js';
import { Loading, Failed } from './state.jsx';

/** Minimal CSV reader for the emitted files: no quoting is used in them. */
function parseCsv(text) {
  const lines = String(text).trim().split(/\r?\n/);
  const head = lines[0].split(',');
  return lines.slice(1).filter(Boolean).map((l) => {
    const cells = l.split(',');
    const row = {};
    head.forEach((h, i) => { row[h] = cells[i]; });
    return row;
  });
}

export default function ScheduleView({ versionId }) {
  const access = useResource(
    (s) => api.versionFile(versionId, 'SCHEDULE_ACCESS.csv', s), [versionId]);
  const [week, setWeek] = useState(null);

  // Indexed once per version, not per render and not per week change. A local
  // control changing the selected week re-reads this index instead of
  // re-parsing or re-fetching anything.
  const index = useMemo(() => {
    if (!access.data) return null;
    const rows = parseCsv(access.data);
    const byWeek = new Map();
    const byActivity = new Map();
    for (const r of rows) {
      const w = Number(r.week);
      if (!byWeek.has(w)) byWeek.set(w, []);
      byWeek.get(w).push(r);
      if (!byActivity.has(r.activity_id)) byActivity.set(r.activity_id, []);
      byActivity.get(r.activity_id).push(r);
    }
    return { rows, byWeek, byActivity, weeks: [...byWeek.keys()].sort((a, b) => a - b) };
  }, [access.data]);

  if (access.loading) return <Loading what="Loading the schedule" />;
  if (access.error) return <Failed error={access.error} onRetry={access.reload} what="load the schedule" />;
  if (!index) return null;

  const shown = week == null ? index.rows : (index.byWeek.get(week) || []);

  return (
    <div style={{ marginTop: 'var(--gap-5)' }}>
      <p className="label">
        {index.rows.length} access slots over {index.weeks.length} weeks.
        week and access_night are accounting indexes for a contract, type and
        week. They are not calendar dates and not shift times.
      </p>

      <div className="weeks" role="group" aria-label="Filter by week">
        <button type="button" className={'week' + (week == null ? ' week-on' : '')}
                onClick={() => setWeek(null)}>all</button>
        {index.weeks.map((w) => (
          <button key={w} type="button"
                  className={'week' + (week === w ? ' week-on' : '')}
                  aria-pressed={week === w}
                  onClick={() => setWeek(w)}>{w}</button>
        ))}
      </div>

      <div className="scroll-x" style={{ marginTop: 'var(--gap-4)' }}>
        <table className="data">
          <caption className="sr-only">
            Scheduled access nights{week != null ? ` in week ${week}` : ''}
          </caption>
          <thead>
            <tr>
              <th scope="col">activity_id</th><th scope="col">access_seq</th>
              <th scope="col">week</th><th scope="col">eclo</th><th scope="col">access_night</th>
            </tr>
          </thead>
          <tbody>
            {shown.slice(0, 400).map((r, i) => (
              <tr key={r.activity_id + '-' + r.access_seq + '-' + i}>
                <td className="id">{r.activity_id}</td>
                <td className="num">{r.access_seq}</td>
                <td className="num">{r.week}</td>
                <td className="num">{r.eclo}</td>
                <td className="num">{r.access_night}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {shown.length > 400 ? (
        <p className="label">Showing the first 400 of {shown.length} rows. Choose a week to narrow it.</p>
      ) : null}
    </div>
  );
}
