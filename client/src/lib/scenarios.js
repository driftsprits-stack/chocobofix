/**
 * Scenario naming.
 *
 * A, B and C are the three policies PS1 requires. They are not categories
 * detected from the data, and all three must always be produced. The letters
 * stay verbatim in API values, provenance, filenames and the `scenario` export
 * column - only the label an operator reads is friendlier.
 *
 * Wording checked against PS1_README.md §2.5.
 */
export const SCENARIOS = ['A', 'B', 'C'];

export const SCENARIO = {
  A: {
    code: 'A',
    name: 'Fixed access',
    tagline: 'Fixed access · A',
    explain: 'Use the normal access supply. Work may finish later. No extra capacity and no early closure or late opening.',
  },
  B: {
    code: 'B',
    name: 'Fixed deadlines',
    tagline: 'Fixed deadlines · B',
    explain: 'Meet the planned completion dates. Extra access nights and early closure or late opening may be needed.',
  },
  C: {
    code: 'C',
    name: 'Balanced',
    tagline: 'Balanced · C',
    explain: 'Balance delay against extra access, within the limits this scenario allows.',
  },
};

export const scenarioName = (code) => SCENARIO[code]?.name ?? code;
export const scenarioTagline = (code) => SCENARIO[code]?.tagline ?? code;
export const scenarioExplain = (code) => SCENARIO[code]?.explain ?? '';

/** ECLO is expanded once, where it is first shown. */
export const ECLO_FULL = 'Early closure / late opening';

/**
 * Week -> calendar date, using the instance's own horizon_start.
 * Returns null when the instance has no mapping, so the caller shows the week
 * number alone rather than inventing a date.
 */
export function weekStartDate(horizonStart, week) {
  if (!horizonStart) return null;
  const d = new Date(horizonStart + 'T00:00:00Z');
  if (Number.isNaN(d.getTime())) return null;
  d.setUTCDate(d.getUTCDate() + (Number(week) - 1) * 7);
  return d;
}

export function formatWeek(horizonStart, week) {
  const d = weekStartDate(horizonStart, week);
  if (!d) return `week ${week}`;
  const s = d.toISOString().slice(0, 10);
  return `week ${week} · ${s}`;
}
