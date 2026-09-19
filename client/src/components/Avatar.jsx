import { useEffect, useState } from 'react';
import { loadPhoto, cachedPhoto } from '../lib/photos.js';

/**
 * A person, shown as their photo when there is one and their initials when
 * there is not. The caller always renders the name beside it: a picture alone
 * does not identify who is responsible for the work.
 */
function initials(name) {
  if (!name) return '?';
  const parts = String(name).split(/[.\s_-]+/).filter(Boolean);
  const take = parts.length >= 2 ? [parts[0], parts[1]] : [parts[0] || '?'];
  return take.map((p) => p[0].toUpperCase()).join('').slice(0, 2);
}

export default function Avatar({ user, size = 24 }) {
  const name = user?.username || user?.name || '';
  const id = user?.id || null;
  const [src, setSrc] = useState(() => cachedPhoto(id));

  useEffect(() => {
    let live = true;
    if (!id) { setSrc(null); return undefined; }
    const known = cachedPhoto(id);
    if (known) { setSrc(known); return undefined; }
    // The server tells us who actually has a photo, so a wall of avatars makes
    // no request at all for the people who do not - rather than discovering it
    // by collecting a 404 each.
    if (user?.photo === false) { setSrc(null); return undefined; }
    loadPhoto(id).then((url) => { if (live) setSrc(url); });
    return () => { live = false; };
  }, [id, user?.rev, user?.photo]);

  return (
    <span className="avatar" style={{ width: size, height: size, fontSize: Math.round(size * 0.42) }}>
      {src ? <img src={src} alt="" width={size} height={size} decoding="async" /> : null}
      {/* Initials sit underneath, so a photo that fails to decode still leaves
          something readable rather than a broken-image icon. */}
      <span className="avatar-initials" aria-hidden="true">{initials(name)}</span>
    </span>
  );
}
