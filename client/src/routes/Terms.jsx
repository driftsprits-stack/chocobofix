import { Link, useLocation } from 'react-router-dom';
import Footer from '../components/Footer.jsx';
import { operator, country, contact } from '../lib/site.js';
const pages = {
  '/terms': {title:'terms.', sections:[
    ['Using the workspace', 'ChocoboFix makes railway access plans from the PS1 input files. Use an account assigned to you. Upload only data that you are allowed to use. Do not attempt to access another person’s account or disrupt the service.'],
    ['Review each plan', 'A generated plan is a planning aid. The local checker applies the rules implemented in this service. It is not an approval to carry out railway work. An authorised person must review the input data, outputs and operational requirements before use.'],
    ['Access and availability', 'Your role controls what you can do. Administrators and approvers can view projects across this workspace. Availability, recovery times and data retention depend on the operator’s deployment. No service-level guarantee is currently provided.'],
    ['Accounts and acceptance', 'The browser sign-in form asks you to accept these terms. If you disagree, do not create an account or use the workspace. Ask your workspace administrator to close an existing account.'],
    ['Charges', 'This application has no payment or billing feature. It does not process purchases or refunds.']
  ]},
  '/privacy': {title:'privacy.', sections:[
    ['Information held', 'The service stores account names, password hashes, roles, session records, optional profile photos, uploaded planning files, generated plans and activity assignments. Audit records describe actions taken in the workspace. Avoid including personal information in planning files unless it is required and authorised.'],
    ['Use and access', 'This information supports sign-in, planning, assignment and audit functions. Project owners, administrators and approvers can view project data. The service is currently designed for one organisation. It does not provide isolation between separate customer organisations.'],
    ['Storage and retention', 'The current service stores its database and files on the operator’s server. Firebase is not connected. There is no automatic account-deletion or retention schedule. The operator must set retention periods and handle deletion requests, including copies held in backups.'],
    ['Requests', 'Contact your workspace administrator to request access, correction or deletion of your information. A deployment must identify its operator and publish the appropriate contact details before public use.']
  ]},
  '/cookies': {title:'cookies.', sections:[
    ['Browser storage', 'This application does not set tracking cookies. It stores a sign-in token in session storage and appearance preferences in local storage. Session storage is normally cleared when the browser tab closes. Preferences remain on the device until you clear them. Signing out clears the application’s sign-in state.'],
    ['Photos and requests', 'Profile photos may be kept in memory while the workspace is open. The service sends authenticated requests to its own backend. No analytics, advertising scripts or third-party media embeds are included in this build.'],
    ['Your choice', 'You can clear stored data in your browser settings. This signs you out and resets your preferences. If the operator adds analytics or other optional storage, it must assess consent requirements and update this page first.']
  ]}
};
export default function Terms() {
 const {pathname} = useLocation(); const page = pages[pathname] || pages['/terms'];
 return <main id="main" className="page shell-main"><Link className="brand" to="/">chocobofix</Link><div className="page-head" style={{marginTop:'4rem'}}><h1 className="h1">{page.title}</h1><p className="sub">Workspace information.<br />Updated 19 September 2026.</p></div><div className="policy-body">{page.sections.map(([h,p])=><section key={h}><h2>{h}</h2><p>{p}</p></section>)}<section><h2>operator</h2>{operator ? <p>{operator}{country ? ` / ${country}` : ''}</p> : <p>Operator details have not yet been configured. Contact your workspace administrator.</p>}{contact && <a href={`mailto:${contact}`}>{contact}</a>}</section></div>{pathname === '/terms' && <div className="actions-row"><Link className="btn" to="/signin" state={{termsAccepted:true}}>agree and continue</Link><Link className="btn" to="/">disagree and leave</Link></div>}<Footer /></main>;
}
