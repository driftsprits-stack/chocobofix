import { useCallback, useEffect, useRef, useState } from 'react';

/**
 * Fetch something, with the obsolete-reply problem solved once.
 *
 * Every run gets an AbortController and a sequence number. A reply from an older
 * run is ignored even if the abort lost the race, so switching project can never
 * leave the previous project's data on screen. `reload` retries after a failure.
 *
 * @param {(signal: AbortSignal) => Promise<any>} fn
 * @param {any[]} deps
 */
export function useResource(fn, deps = [], { enabled = true } = {}) {
  const [state, setState] = useState({ data: null, error: null, loading: enabled });
  const seq = useRef(0);
  const fnRef = useRef(fn);
  fnRef.current = fn;

  const run = useCallback(() => {
    if (!enabled) { setState({ data: null, error: null, loading: false }); return () => {}; }
    const mine = ++seq.current;
    const ac = new AbortController();
    setState((s) => ({ ...s, loading: true, error: null }));
    fnRef.current(ac.signal)
      .then((data) => { if (mine === seq.current && !ac.signal.aborted) setState({ data, error: null, loading: false }); })
      .catch((error) => {
        if (error.name === 'AbortError' || ac.signal.aborted || mine !== seq.current) return;
        setState({ data: null, error, loading: false });
      });
    return () => ac.abort();
  }, [enabled, ...deps]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => run(), [run]);

  return { ...state, reload: run };
}
