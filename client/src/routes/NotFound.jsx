import { Link } from 'react-router-dom';
import Footer from '../components/Footer.jsx';
export default function NotFound() {
 return <main id="main" className="page shell-main"><Link className="brand" to="/">chocobofix</Link><div className="page-head" style={{marginTop:'5rem'}}><h1 className="display">404.</h1><div><p>We could not find that page.</p><div className="actions-row"><Link className="btn" to="/">start page</Link><Link className="btn" to="/overview">workspace</Link></div></div></div><Footer /></main>;
}
