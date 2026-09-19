import { useEffect, useRef, useState } from 'react';

/**
 * Decorative media layer behind the sign-in hero.
 *
 * No asset ships with this build. Drop a licensed clip at
 * `client/public/media/login.mp4` (with a poster at `login-poster.jpg`) and it
 * is picked up automatically; until then the static gradient-free surface below
 * is the design, not a placeholder for something missing.
 *
 * A GIF would also work in the same slot, but a muted looping video is
 * preferred: it can be paused, a GIF cannot.
 *
 * Nothing here blocks sign-in. The video is only attached after the form has
 * mounted, is muted and inline, pauses when off-screen or when the tab is
 * hidden, and falls back to the poster on any failure.
 */
const SRC = '/media/login.mp4';
const POSTER = '/media/login-poster.jpg';

export default function LoginMedia() {
  const ref = useRef(null);
  const [usable, setUsable] = useState(false);
  const [paused, setPaused] = useState(false);

  useEffect(() => {
    const reduced = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ||
      document.documentElement.getAttribute('data-motion') === 'reduced';
    if (reduced) return undefined;

    // Only ask for the asset once the form is already interactive.
    const id = window.setTimeout(() => {
      fetch(SRC, { method: 'HEAD' })
        .then((r) => setUsable(r.ok))
        .catch(() => setUsable(false));
    }, 0);
    return () => window.clearTimeout(id);
  }, []);

  useEffect(() => {
    const v = ref.current;
    if (!v || !usable) return undefined;
    const onVis = () => { if (document.hidden) v.pause(); else if (!paused) v.play().catch(() => {}); };
    document.addEventListener('visibilitychange', onVis);
    const io = new IntersectionObserver(([e]) => {
      if (!e.isIntersecting) v.pause(); else if (!paused) v.play().catch(() => {});
    });
    io.observe(v);
    return () => { document.removeEventListener('visibilitychange', onVis); io.disconnect(); };
  }, [usable, paused]);

  if (!usable) return <div className="login-media login-media-static" aria-hidden="true" />;

  return (
    <div className="login-media" aria-hidden="true">
      <video
        ref={ref}
        muted
        loop
        playsInline
        autoPlay
        preload="none"
        poster={POSTER}
        onError={() => setUsable(false)}
      >
        <source src={SRC} type="video/mp4" />
      </video>
      <button type="button" className="media-pause"
              aria-pressed={paused}
              onClick={() => {
                const v = ref.current;
                if (!v) return;
                if (paused) { v.play().catch(() => {}); setPaused(false); }
                else { v.pause(); setPaused(true); }
              }}>
        {paused ? 'Play background' : 'Pause background'}
      </button>
    </div>
  );
}
