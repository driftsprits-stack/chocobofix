import { describe, it, expect } from 'vitest';
import { INSTANCE_FILES, matchFiles } from '../instanceFiles.js';

const f = (name, size = 10) => ({ name, size });

describe('matchFiles', () => {
  it('reports every file missing when nothing is chosen', () => {
    const r = matchFiles([]);
    expect(r.missing).toEqual(INSTANCE_FILES);
    expect(r.extra).toEqual([]);
  });

  it('matches all eight by exact name', () => {
    const r = matchFiles(INSTANCE_FILES.map((n) => f(n)));
    expect(r.missing).toEqual([]);
    expect([...r.matched.keys()]).toEqual(INSTANCE_FILES);
  });

  it('matches a re-downloaded copy by its leading number', () => {
    // Browsers name a second download "01_LINES (1).csv"; it is still the file.
    const r = matchFiles([f('01_LINES (1).csv')]);
    expect(r.matched.get('01_LINES.csv')).toBeTruthy();
    expect(r.missing).not.toContain('01_LINES.csv');
  });

  it('does not let a duplicate displace an exact match', () => {
    const r = matchFiles([f('01_LINES.csv'), f('01_LINES (1).csv')]);
    expect(r.matched.get('01_LINES.csv').name).toBe('01_LINES.csv');
    expect(r.extra).toEqual(['01_LINES (1).csv']);
  });

  it('lists unrelated files as extra rather than silently dropping them', () => {
    const r = matchFiles([f('notes.txt')]);
    expect(r.extra).toEqual(['notes.txt']);
    expect(r.missing).toEqual(INSTANCE_FILES);
  });
});
