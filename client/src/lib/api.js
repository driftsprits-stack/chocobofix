/** Bounded, session-scoped requests. No mutation is automatically retried. */
const MAX_CONCURRENT = 4;
const MAX_CACHE = 64;
let active = 0;
const waiting = [];
const immutable = new Map();
const inflight = new Map();
let token = null;
let epoch = 0;
let sessionController = new AbortController();

export class ApiError extends Error {
  constructor(message, status, body) {
    super(message); this.name = 'ApiError'; this.status = status; this.body = body;
    this.retryable = status === 0 || status >= 500 || status === 429;
  }
}
const aborted = () => new DOMException('The request was cancelled.', 'AbortError');
function acquire(signal) {
  if (signal.aborted) return Promise.reject(aborted());
  if (active < MAX_CONCURRENT) { active++; return Promise.resolve(); }
  return new Promise((resolve, reject) => {
    const entry = { resolve, signal, cancel: null };
    entry.cancel = () => { const i = waiting.indexOf(entry); if (i >= 0) waiting.splice(i, 1); reject(aborted()); };
    signal.addEventListener('abort', entry.cancel, { once: true });
    waiting.push(entry);
  });
}
function release() {
  active--;
  const next = waiting.shift();
  if (next) { next.signal.removeEventListener('abort', next.cancel); active++; next.resolve(); }
}
export function setToken(t) {
  if (t === token) return;
  token = t; epoch++; sessionController.abort(); sessionController = new AbortController();
  immutable.clear(); inflight.clear();
}
export function getToken() { return token; }
export function clearCache(prefix = '') {
  for (const key of immutable.keys()) if (key.startsWith(prefix)) immutable.delete(key);
}
export function cacheStats() { return { cached: immutable.size, inflight: inflight.size, active, queued: waiting.length }; }

async function raw(path, { method = 'GET', body, form, signal, timeoutMs = 30000 } = {}, authToken, scope) {
  const controller = new AbortController();
  const abort = () => controller.abort();
  const sessionSignal = sessionController.signal;
  signal?.addEventListener('abort', abort, { once: true });
  sessionSignal.addEventListener('abort', abort, { once: true });
  if (signal?.aborted || sessionSignal.aborted) abort();
  let timedOut = false;
  const timer = setTimeout(() => { timedOut = true; abort(); }, timeoutMs);
  let acquired = false;
  try {
    await acquire(controller.signal); acquired = true;
    if (controller.signal.aborted || scope !== epoch) throw aborted();
    const headers = { 'X-Correlation-Id': 'ui-' + (globalThis.crypto?.randomUUID?.() || Math.random().toString(36).slice(2)) };
    if (authToken) headers.Authorization = 'Bearer ' + authToken;
    let payload = body;
    if (form) { headers['Content-Type'] = 'application/x-www-form-urlencoded'; payload = new URLSearchParams(form).toString(); }
    const res = await fetch(path, { method, headers, body: payload, signal: controller.signal, cache: 'no-store', credentials: 'same-origin' });
    const ct = res.headers.get('content-type') || '';
    const parsed = ct.includes('json') ? await res.json() : await res.text();
    if (scope !== epoch || controller.signal.aborted) throw aborted();
    if (!res.ok) {
      if (res.status === 401 && authToken && typeof window !== 'undefined') window.dispatchEvent(new Event('chocobofix:session-expired'));
      throw new ApiError(parsed?.error || res.statusText || 'The request failed.', res.status, parsed);
    }
    return parsed;
  } catch (error) {
    if (timedOut) throw new ApiError('The request took too long. Check its status before you retry a change.', 0, null);
    if (error.name === 'AbortError' || error instanceof ApiError) throw error;
    throw new ApiError('The service could not be reached or returned an invalid response. Try again.', 0, null);
  } finally {
    clearTimeout(timer); signal?.removeEventListener('abort', abort); sessionSignal.removeEventListener('abort', abort);
    if (acquired) release();
  }
}
export function request(path, opts = {}) {
  const { cacheKey, dedupeKey, signal } = opts;
  if (signal?.aborted) return Promise.reject(aborted());
  const scope = epoch;
  const read = !opts.method || opts.method === 'GET';
  // Independently cancellable scopes do not share a transport. This also makes
  // React StrictMode cleanup safe: an aborted mount cannot poison its successor.
  const key = read && !signal ? `${scope}:${cacheKey || dedupeKey || path}` : null;
  if (cacheKey && immutable.has(cacheKey)) return Promise.resolve(immutable.get(cacheKey));
  if (key && inflight.has(key)) return inflight.get(key);
  const promise = raw(path, opts, token, scope).then(value => {
    if (scope !== epoch) throw aborted();
    if (cacheKey) {
      if (immutable.size >= MAX_CACHE) immutable.delete(immutable.keys().next().value);
      immutable.set(cacheKey, value);
    }
    return value;
  }).finally(() => { if (key && inflight.get(key) === promise) inflight.delete(key); });
  if (key) inflight.set(key, promise);
  return promise;
}

// ------------------------------------------------------------- endpoints

export const api = {
  health: (signal) => request('/api/v1/health', { signal }),
  // Unwraps {user:{...}}. Returning the envelope would double-wrap the restored
  // session and make every `user.can.*` check read undefined after a refresh.
  me: (signal) => request('/api/v1/auth/me', { signal }).then((r) => r.user ?? r),

  bootstrap: (username, password) =>
    request('/api/v1/bootstrap', { method: 'POST', form: { username, password } }),
  login: (username, password) =>
    request('/api/v1/auth/login', { method: 'POST', form: { username, password } }),
  logout: () => request('/api/v1/auth/logout', { method: 'POST' }),

  projects: (signal, before) => request(`/api/v1/projects${before ? `?before=${before}` : ''}`, { signal }),
  project: (pid, signal) => request(`/api/v1/projects/${encodeURIComponent(pid)}`, { signal }),
  createProject: (name) => request('/api/v1/projects', { method: 'POST', form: { name } }),

  instances: (pid, signal) =>
    request(`/api/v1/projects/${pid}/instances`, { signal, dedupeKey: `instances:${pid}` }),
  loadDemo: (pid) => request(`/api/v1/projects/${pid}/instances/demo`, { method: 'POST' }),
  uploadInstance: (pid, formData, signal) =>
    request(`/api/v1/projects/${pid}/instances`, { method: 'POST', body: formData, signal }),

  instanceDetail: (iid, signal) =>
    // An instance is immutable once uploaded: its contents are hashed and never
    // rewritten, so this is safe to keep.
    request(`/api/v1/instances/${iid}/detail`, { signal, cacheKey: `instance:${iid}` }),

  createJob: (pid, instanceId, scenario, seconds) =>
    request(`/api/v1/projects/${pid}/jobs`, {
      method: 'POST', form: { instance_id: instanceId, scenario, seconds },
    }),
  job: (id, signal) => request(`/api/v1/jobs/${id}`, { signal, dedupeKey: `job:${id}` }),
  jobLog: (id, signal) => request(`/api/v1/jobs/${id}/log`, { signal }),
  cancelJob: (id) => request(`/api/v1/jobs/${id}/cancel`, { method: 'POST' }),

  versions: (pid, signal) =>
    request(`/api/v1/projects/${pid}/versions`, { signal, dedupeKey: `versions:${pid}` }),
  validation: (vid, signal) =>
    request(`/api/v1/versions/${vid}/validation`, { signal, cacheKey: `validation:${vid}` }),
  versionFile: (vid, name, signal) =>
    request(`/api/v1/versions/${vid}/files/${name}`, { signal, cacheKey: `file:${vid}:${name}` }),
  approve: (vid) => request(`/api/v1/versions/${vid}/approve`, { method: 'POST' }),

  audit: (pid, signal) => request(`/api/v1/projects/${pid}/audit`, { signal }),

  // Coordinator assignments, scoped to project + instance + activity id.
  // Mutable, so never cached; the dedupe key still collapses concurrent reads.
  assignments: (pid, iid, signal) =>
    request(`/api/v1/projects/${pid}/assignments?instance_id=${iid}`,
            { signal, dedupeKey: `assign:${pid}:${iid}` }),
  assign: (pid, form) =>
    request(`/api/v1/projects/${pid}/assignments`, { method: 'POST', form }),
  users: (signal) => request('/api/v1/users', { signal, dedupeKey: 'users' }),
  createUser: (username, password, role) =>
    request('/api/v1/users', { method: 'POST', form: { username, password, role } }),
  setUserDisabled: (id, disabled) =>
    request(`/api/v1/users/${encodeURIComponent(id)}/disable`, {
      method: 'POST', form: { disabled: disabled ? '1' : '0' },
    }),

  // Profile photo. A body with no file part means "remove mine".
  uploadPhoto: (formData) => request('/api/v1/profile/photo', { method: 'POST', body: formData }),
  removePhoto: () => request('/api/v1/profile/photo', { method: 'POST' }),
  repair: (iid, form) => request(`/api/v1/instances/${iid}/repair`, { method: 'POST', form }),
};
