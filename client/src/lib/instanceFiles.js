/** The eight files PS1 requires, in the order they are listed upstream. */
export const INSTANCE_FILES = [
  '01_LINES.csv', '02_STATIONS.csv', '03_SECTORS.csv', '04_LOCATION_SUPPLY.csv',
  '05_BUFFER_LOCATION.csv', '06_PARAMETERS.csv', '07_PROJECT_DETAILS.csv',
  '08_ACTIVITY_DETAILS.csv',
];

/**
 * Match dropped or picked files to the eight names, by exact name first and
 * then by the leading number, so `01_LINES (1).csv` from a second download
 * still lands in the right slot.
 */
export function matchFiles(fileList) {
  const wanted = new Map(INSTANCE_FILES.map((n) => [n, null]));
  const extra = [];
  for (const f of fileList) {
    if (wanted.has(f.name) && !wanted.get(f.name)) { wanted.set(f.name, f); continue; }
    const m = /^(\d{2})[_ ]/.exec(f.name);
    const slot = m && INSTANCE_FILES.find((n) => n.startsWith(m[1] + '_'));
    if (slot && !wanted.get(slot)) wanted.set(slot, f);
    else extra.push(f.name);
  }
  return { matched: wanted, extra, missing: INSTANCE_FILES.filter((n) => !wanted.get(n)) };
}
