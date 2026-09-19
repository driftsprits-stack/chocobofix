import { Link } from 'react-router-dom';
export default function Footer() {
  return <footer className="workspace-footer"><span>© {new Date().getFullYear()} chocobofix</span><nav aria-label="Legal"><Link to="/privacy">privacy</Link><Link to="/terms">terms</Link><Link to="/cookies">cookies</Link></nav></footer>;
}
