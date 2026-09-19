import { useEffect, useRef, useState } from 'react';

/**
 * A small rotating line beside the sign-in form.
 *
 * Rules it follows, all of them about not getting in the way:
 *  - the form is never gated on this; it is a sibling, not a wrapper;
 *  - the box reserves height for the longest message, so nothing moves;
 *  - the timer stops when the tab is hidden and is cleared on unmount;
 *  - it pauses while the user is interacting with the form;
 *  - reduced motion means one static message and no timer at all;
 *  - aria-hidden, because a headline changing every seven seconds is noise to
 *    a screen reader, not information.
 */
const MESSAGES = [
  'See which work gets track access.',
  'Find the plan for your project.',
  'Check where each activity is scheduled.',
  'See who coordinates each job.',
  'Compare access plans before you export.',
];

const PERIOD = 7000;

export default function RotatingMessage({ paused = false }) {
  const [i, setI] = useState(0);
  const [fading, setFading] = useState(false);
  const timer = useRef(null);

  const reduced = typeof window !== 'undefined' &&
    (window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ||
     document.documentElement.getAttribute('data-motion') === 'reduced');

  const [userPaused, setUserPaused] = useState(false);
  const stopped = reduced || paused || userPaused;

  useEffect(() => {
    if (stopped) return undefined;

    const tick = () => {
      if (document.hidden) return;            // no work while the tab is hidden
      setFading(true);
      window.setTimeout(() => {
        setI((n) => (n + 1) % MESSAGES.length);
        setFading(false);
      }, 200);
    };

    timer.current = window.setInterval(tick, PERIOD);
    const onVis = () => {
      // Restart cleanly rather than firing a burst of missed ticks.
      window.clearInterval(timer.current);
      if (!document.hidden) timer.current = window.setInterval(tick, PERIOD);
    };
    document.addEventListener('visibilitychange', onVis);

    return () => {
      window.clearInterval(timer.current);
      document.removeEventListener('visibilitychange', onVis);
    };
  }, [stopped]);

  return (
    <div className="rotator">
      {/* Height is reserved for the longest line, including a longer
          translation, so the form below never shifts. */}
      <p className={'rotator-msg' + (fading ? ' is-fading' : '')} aria-hidden="true">
        {reduced ? MESSAGES[0] : MESSAGES[i]}
      </p>
      {!reduced ? (
        <button type="button" className="rotator-pause"
                aria-pressed={userPaused}
                onClick={() => setUserPaused((p) => !p)}>
          {userPaused ? 'Resume' : 'Pause'}
        </button>
      ) : null}
    </div>
  );
}
