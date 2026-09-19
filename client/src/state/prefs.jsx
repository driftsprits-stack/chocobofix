import { createContext, useContext, useEffect, useMemo, useState } from 'react';

/**
 * Appearance, text size, motion and language.
 *
 * Appearance defaults to "system": nothing is stamped on the root element and
 * the palette follows prefers-color-scheme. An explicit choice stamps
 * data-theme and wins in both directions.
 */

const KEY = 'chocobofix.prefs';
const DEFAULTS = { theme: 'light', textSize: 'normal', motion: 'system', lang: 'en' };

const Ctx = createContext(null);

function load() {
  try {
    const saved = JSON.parse(localStorage.getItem(KEY) || '{}');
    const result = { ...DEFAULTS };
    const allowed = { theme: ['light', 'dark', 'system'], textSize: ['normal', 'large', 'larger'], motion: ['system', 'reduced'] };
    for (const [key, values] of Object.entries(allowed)) if (values.includes(saved?.[key])) result[key] = saved[key];
    return result;
  }
  catch { return { ...DEFAULTS }; }
}

export function PrefsProvider({ children }) {
  const [prefs, setPrefs] = useState(load);

  useEffect(() => {
    const root = document.documentElement;
    // "system" stamps nothing, so the media query decides.
    if (prefs.theme === 'system') root.removeAttribute('data-theme');
    else root.setAttribute('data-theme', prefs.theme);

    if (prefs.motion === 'system') root.removeAttribute('data-motion');
    else root.setAttribute('data-motion', prefs.motion);

    root.setAttribute('data-text', prefs.textSize);
    root.setAttribute('lang', 'en');

    try { localStorage.setItem(KEY, JSON.stringify(prefs)); } catch { /* private mode */ }
  }, [prefs]);

  const value = useMemo(
    () => ({ prefs, set: (k, v) => setPrefs((p) => ({ ...p, [k]: v })) }),
    [prefs]
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function usePrefs() {
  const v = useContext(Ctx);
  if (!v) throw new Error('usePrefs used outside PrefsProvider');
  return v;
}
