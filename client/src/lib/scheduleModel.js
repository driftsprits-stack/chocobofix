/** Keep every access. A coordinator is a grouping key, never an activity key. */
export function filterRows(rows, { query = '', person = '', week = '', access = '', priority = '', meId } = {}) {
  const needle = query.trim().toLowerCase();
  const owner = person.startsWith('me:') ? String(meId) : person;
  return rows.filter(r =>
    (!owner || (owner === 'unassigned' ? !r.coordinator?.coordinator_id : String(r.coordinator?.coordinator_id) === owner)) &&
    (!week || r.week === Number(week)) &&
    (!access || (access === 'eclo' ? r.eclo : !r.eclo)) &&
    (!priority || Number(r.priority) === Number(priority)) &&
    (!needle || [r.activityId, r.contract, r.location, r.from, r.to, r.coordinator?.coordinator].some(v => String(v || '').toLowerCase().includes(needle)))
  );
}
export function groupRows(rows, by) {
  const groups = new Map();
  for (const row of rows) {
    const key = by === 'person' ? (row.coordinator?.coordinator || 'Unassigned') :
      by === 'contract' ? row.contract : 'all scheduled work';
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(row);
  }
  return [...groups.entries()].sort(([a], [b]) => a.localeCompare(b, undefined, { numeric: true }));
}
export function activityRows(rows) {
  const groups = new Map();
  for (const row of rows) {
    const key = `${row.projectId}:${row.activityId}`;
    if (!groups.has(key)) groups.set(key, { key, first: row, byWeek: new Map() });
    const item = groups.get(key);
    if (!item.byWeek.has(row.week)) item.byWeek.set(row.week, []);
    item.byWeek.get(row.week).push(row);
  }
  return [...groups.values()].sort((a,b) => a.first.activityId.localeCompare(b.first.activityId, undefined, { numeric: true }));
}
export function sortRows(rows, field = 'week', direction = 'asc') {
  return [...rows].sort((a, b) => {
    const x = a[field] ?? '', y = b[field] ?? '';
    const result = typeof x === 'number' && typeof y === 'number' ? x - y : String(x).localeCompare(String(y), undefined, { numeric: true });
    return (direction === 'desc' ? -result : result) || a.activityId.localeCompare(b.activityId);
  });
}
