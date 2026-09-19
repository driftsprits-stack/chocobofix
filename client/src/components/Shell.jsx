import { useEffect, useRef, useState } from 'react';
import { NavLink, Link, useNavigate } from 'react-router-dom';
import Footer from './Footer.jsx';
import { useAuth } from '../state/auth.jsx';

export default function Shell({ children, crumb, className = '' }) {
  const { user, signOut } = useAuth();
  const nav = useNavigate();
  const [menu, setMenu] = useState(false);
  const [mobile, setMobile] = useState(false);
  const account = useRef(null);
  const toggle = useRef(null);
  useEffect(() => {
    if (!menu) return;
    const close = e => { if (!account.current?.contains(e.target)) setMenu(false); };
    const key = e => { if (e.key === 'Escape') { setMenu(false); toggle.current?.focus(); } };
    document.addEventListener('pointerdown', close);
    document.addEventListener('keydown', key);
    return () => { document.removeEventListener('pointerdown', close); document.removeEventListener('keydown', key); };
  }, [menu]);
  return <>
    <header className="shell-head"><div className="shell-head-inner page">
      <Link to="/overview" className="brand">chocobofix</Link>
      <button className="mobile-toggle" aria-expanded={mobile} aria-controls="main-nav" onClick={() => setMobile(!mobile)}>menu {mobile ? '−' : '+'}</button>
      <nav id="main-nav" className={`shell-nav ${mobile ? 'is-open' : ''}`} aria-label="Main" onClick={() => setMobile(false)}><NavLink to="/overview" className="navlink">today</NavLink><NavLink to="/projects" className="navlink">projects</NavLink><NavLink to="/schedules" className="navlink">schedules</NavLink><NavLink to="/settings" className="navlink">settings</NavLink></nav>
      <div className="account" ref={account}><button ref={toggle} className="account-btn" aria-expanded={menu} aria-controls="account-links" onClick={() => setMenu(!menu)}>{user?.username || 'account'}</button>
        {menu && <div className="menu" id="account-links"><Link to="/settings" onClick={() => setMenu(false)}>profile & settings</Link><Link to="/terms" onClick={() => setMenu(false)}>terms</Link><button onClick={async () => { setMenu(false); await signOut(); nav('/'); }}>sign out</button></div>}
      </div>
    </div></header>
    <main id="main" className={`page shell-main ${className}`}>{crumb && <p className="crumb">{crumb}</p>}{children}<Footer /></main>
  </>;
}
