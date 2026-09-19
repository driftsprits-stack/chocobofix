import { getToken } from './api.js';

/**
 * Profile photos, fetched with the session token.
 *
 * An <img src="/api/v1/users/3/photo"> cannot carry an Authorization header, so
 * the browser sent those requests unauthenticated and every one came back 401 -
 * avatars could never load, and the console filled with failures. The bytes are
 * fetched here instead and handed to the <img> as an object URL.
 *
 * Results are cached per user id, including the "no photo" answer, so a wall of
 * avatars makes one request per person rather than one per row.
 */
const cache = new Map();   // id -> { url } | { none: true }
const inflight = new Map();
let generation = 0;

export function cachedPhoto(id) {
  const hit = cache.get(id);
  return hit && hit.url ? hit.url : null;
}

export function loadPhoto(id) {
  if (!id) return Promise.resolve(null);
  if (cache.has(id)) return Promise.resolve(cache.get(id).url || null);
  if (inflight.has(id)) return inflight.get(id);

  const token = getToken();
  const scope = generation;
  if (!token) return Promise.resolve(null);

  const p = fetch(`/api/v1/users/${id}/photo`, {
    headers: { Authorization: 'Bearer ' + token }, cache: 'no-store',
  })
    .then(async (r) => {
      if (scope !== generation || token !== getToken()) return null;
      if (!r.ok) { cache.set(id, { none: true }); return null; }
      const blob = await r.blob();
      if (scope !== generation || token !== getToken()) return null;
      const url = URL.createObjectURL(blob);
      cache.set(id, { url });
      return url;
    })
    .catch(() => null)
    .finally(() => { if (inflight.get(id) === p) inflight.delete(id); });

  inflight.set(id, p);
  return p;
}

/** Called after a photo is replaced or removed, and on sign-out. */
export function forgetPhotos(id) {
  generation++; inflight.clear();
  const drop = (k) => {
    const v = cache.get(k);
    if (v && v.url) URL.revokeObjectURL(v.url);
    cache.delete(k);
  };
  if (id) drop(id); else [...cache.keys()].forEach(drop);
}
