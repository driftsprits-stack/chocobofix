import { Link } from 'react-router-dom';
import './Landing.css';
import Footer from '../components/Footer.jsx';

/* The four beats of the demonstration, in the order an operator lives them. */
const BEATS = [
  { n: '01', t: 'supply drops',   d: 'A location offers fewer access slots for one week.' },
  { n: '02', t: 'see what it hits', d: 'The activities and dependencies that used those slots.' },
  { n: '03', t: 're-plan',        d: 'A new schedule that changes as few activities as it can.' },
  { n: '04', t: 'check it',       d: 'The files are read back and checked against the rules.' },
];

export default function Landing() {
  return (
    <main id="main" className="lede page">
      <header className="masthead reveal" style={{ '--d': '0ms' }}>
        <Link className="brand" to="/">chocobofix</Link>
        <nav>
          <Link to="/terms" className="label">terms</Link>
        </nav>
      </header>

      <div className="lede-body">
        <div>
          <h1 className="display reveal" style={{ '--d': '60ms' }}>
            which work<br />gets<br />the track
          </h1>
        </div>

        <div className="lede-aside">
          <hr className="rule draw" style={{ '--d': '380ms' }} />
          <p className="measure reveal" style={{ '--d': '520ms', marginTop: 'var(--gap-4)' }}>
            Turn eight PS1 input files into a railway access plan. Compare three scenarios and check the results.
          </p>

          {/* The primary actions carry no entrance animation at all. An
              operator must be able to see and press them on the first frame;
              fading them in over half a second is decoration charged to the
              person in a hurry. */}
          <div className="actions">
            <Link className="btn" to="/workspace">
              open workspace
            </Link>
            <Link className="btn btn-quiet" to="/sample">
              try sample
            </Link>
          </div>
          <p className="label" style={{ marginTop: 'var(--gap-3)' }}>
            the sample uses the public PS1 dataset. it needs an account.
          </p>
        </div>
      </div>

      <section className="beats reveal" style={{ '--d': '760ms' }} aria-label="How a disruption is handled">
        {BEATS.map((b) => (
          <article className="beat" key={b.n}>
            <span className="beat-n">{b.n}</span>
            <div>
              <h2 className="label" style={{ color: 'var(--ink)' }}>{b.t}</h2>
              <p className="muted" style={{ fontSize: 'var(--fs-label)', maxWidth: '34ch' }}>{b.d}</p>
            </div>
          </article>
        ))}
      </section>
      <Footer />
    </main>
  );
}
