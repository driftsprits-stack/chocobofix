import { useEffect, useState } from 'react';

const SHOW_AFTER = 560;

export default function BackToTop() {
  const [visible, setVisible] = useState(() => window.scrollY > SHOW_AFTER);

  useEffect(() => {
    const update = () => setVisible(window.scrollY > SHOW_AFTER);
    update();
    window.addEventListener('scroll', update, { passive: true });
    return () => window.removeEventListener('scroll', update);
  }, []);

  if (!visible) return null;

  const goToTop = () => {
    const reduced = document.documentElement.dataset.motion === 'reduced'
      || window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    window.scrollTo({ top: 0, behavior: reduced ? 'auto' : 'smooth' });
  };

  return <button type="button" className="back-to-top" onClick={goToTop} aria-label="Back to top">
    <span aria-hidden="true">↑</span> top
  </button>;
}
