import { createContext, useContext, useEffect, useMemo, useState, useCallback } from 'react';
import { api, setToken, clearCache } from '../lib/api.js';
import { forgetPhotos } from '../lib/photos.js';

const Ctx = createContext(null);
const KEY = 'chocobofix.token';

export function AuthProvider({ children }) {
  const [user, setUser] = useState(null);
  const [ready, setReady] = useState(false);
  const [needsBootstrap, setNeedsBootstrap] = useState(false);

  // Restore a session across a refresh. sessionStorage, not localStorage: the
  // token dies with the tab rather than sitting on disk.
  useEffect(() => {
    const ac = new AbortController();
    (async () => {
      const saved = sessionStorage.getItem(KEY);
      if (saved) setToken(saved);
      try {
        const h = await api.health(ac.signal);
        setNeedsBootstrap(!!h.needs_bootstrap);
        if (saved) {
          try { const restored = await api.me(ac.signal); if (!ac.signal.aborted) setUser(restored); }
          catch (error) { if (error.name !== 'AbortError') { sessionStorage.removeItem(KEY); setToken(null); } }
        }
      } catch { /* offline: the sign-in screen will say so */ }
      finally { if (!ac.signal.aborted) setReady(true); }
    })();
    return () => ac.abort();
  }, []);

  useEffect(() => {
    const expire = () => {
      sessionStorage.removeItem(KEY); setToken(null); setUser(null); clearCache(); forgetPhotos();
    };
    window.addEventListener('chocobofix:session-expired', expire);
    return () => window.removeEventListener('chocobofix:session-expired', expire);
  }, []);

  const signIn = useCallback(async (username, password, { bootstrap = false } = {}) => {
    if (bootstrap) await api.bootstrap(username, password);
    const r = await api.login(username, password);
    sessionStorage.setItem(KEY, r.token);
    setToken(r.token);
    setUser(r.user);
    setNeedsBootstrap(false);
    return r.user;
  }, []);

  const signOut = useCallback(async () => {
    try { await api.logout(); } catch { /* the local session goes either way */ }
    sessionStorage.removeItem(KEY);
    setToken(null);
    setUser(null);
    clearCache();
    forgetPhotos();
  }, []);

  const value = useMemo(
    () => ({ user, ready, needsBootstrap, signIn, signOut }),
    [user, ready, needsBootstrap, signIn, signOut]
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useAuth() {
  const v = useContext(Ctx);
  if (!v) throw new Error('useAuth used outside AuthProvider');
  return v;
}
