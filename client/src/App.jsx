import { lazy, Suspense } from 'react';
import { BrowserRouter, Routes, Route, Navigate, useParams } from 'react-router-dom';
import PageMeta from './components/PageMeta.jsx';
import BackToTop from './components/BackToTop.jsx';
import Landing from './routes/Landing.jsx';
import { AuthProvider } from './state/auth.jsx';
import { PrefsProvider } from './state/prefs.jsx';

const Overview = lazy(() => import('./routes/Overview.jsx'));
const SignIn        = lazy(() => import('./routes/SignIn.jsx'));
const Projects      = lazy(() => import('./routes/Projects.jsx'));
const ProjectUpload = lazy(() => import('./routes/ProjectUpload.jsx'));
const ProjectDetail = lazy(() => import('./routes/ProjectDetail.jsx'));
const Schedules     = lazy(() => import('./routes/Schedules.jsx'));
const Sample        = lazy(() => import('./routes/Sample.jsx'));
const Terms         = lazy(() => import('./routes/Terms.jsx'));
const Settings      = lazy(() => import('./routes/Settings.jsx'));
const NotFound      = lazy(() => import('./routes/NotFound.jsx'));

/** /workspace/:pid was the old project address; keep the link working. */
function LegacyProjectRedirect() {
  const { pid } = useParams();
  return <Navigate to={`/projects/${pid}`} replace />;
}

export default function App() {
  return (
    <PrefsProvider>
      <AuthProvider>
        <BrowserRouter>
          <PageMeta />
          <a className="skip-link" href="#main">Skip to content</a>
          <Suspense fallback={<p className="page note" style={{ paddingTop: '2rem' }}>Loading…</p>}>
            <Routes>
              <Route path="/" element={<Landing />} />
              <Route path="/signin" element={<SignIn />} />

              <Route path="/overview" element={<Overview />} />
              <Route path="/projects" element={<Projects />} />
              <Route path="/projects/:pid" element={<ProjectDetail />} />
              <Route path="/projects/:pid/upload" element={<ProjectUpload />} />

              <Route path="/schedules" element={<Schedules />} />

              {/* Old addresses still resolve. */}
              <Route path="/workspace" element={<Navigate to="/overview" replace />} />
              <Route path="/workspace/:pid" element={<LegacyProjectRedirect />} />

              <Route path="/sample" element={<Sample />} />
              <Route path="/terms" element={<Terms />} />
              <Route path="/privacy" element={<Terms />} />
              <Route path="/cookies" element={<Terms />} />
              <Route path="/settings" element={<Settings />} />
              <Route path="*" element={<NotFound />} />
            </Routes>
            <BackToTop />
          </Suspense>
        </BrowserRouter>
      </AuthProvider>
    </PrefsProvider>
  );
}
