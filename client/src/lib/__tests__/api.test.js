import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { request, clearCache, cacheStats, setToken } from '../api.js';

describe('request layer', () => {
  beforeEach(() => { clearCache(); setToken(null); });
  afterEach(() => { vi.restoreAllMocks(); });

  const jsonResponse = (body) => ({
    ok: true, status: 200,
    headers: { get: () => 'application/json' },
    json: async () => body,
  });

  it('shares one in-flight request between concurrent callers', async () => {
    let calls = 0;
    global.fetch = vi.fn(async () => { calls++; return jsonResponse({ n: 1 }); });
    const [a, b, c] = await Promise.all([
      request('/api/v1/thing', { dedupeKey: 'thing' }),
      request('/api/v1/thing', { dedupeKey: 'thing' }),
      request('/api/v1/thing', { dedupeKey: 'thing' }),
    ]);
    expect(calls).toBe(1);
    expect(a).toEqual(b);
    expect(b).toEqual(c);
  });

  it('caches an immutable result and does not refetch it', async () => {
    let calls = 0;
    global.fetch = vi.fn(async () => { calls++; return jsonResponse({ v: 'plan' }); });
    await request('/api/v1/versions/1/files/RESULTS.csv', { cacheKey: 'file:1:RESULTS.csv' });
    await request('/api/v1/versions/1/files/RESULTS.csv', { cacheKey: 'file:1:RESULTS.csv' });
    expect(calls).toBe(1);
    expect(cacheStats().cached).toBe(1);
  });

  it('does not cache a failure, so a retry really retries', async () => {
    let calls = 0;
    global.fetch = vi.fn(async () => {
      calls++;
      if (calls === 1) return { ok: false, status: 500, statusText: 'boom',
                                headers: { get: () => 'application/json' }, json: async () => ({}) };
      return jsonResponse({ ok: true });
    });
    await expect(request('/api/v1/x', { cacheKey: 'x' })).rejects.toThrow();
    const second = await request('/api/v1/x', { cacheKey: 'x' });
    expect(calls).toBe(2);
    expect(second).toEqual({ ok: true });
  });

  it('marks 5xx and 429 retryable but not 401', async () => {
    const mk = (status) => {
      global.fetch = vi.fn(async () => ({ ok: false, status, statusText: 's',
        headers: { get: () => 'application/json' }, json: async () => ({}) }));
      return request('/api/v1/e' + status).catch((e) => e);
    };
    expect((await mk(503)).retryable).toBe(true);
    expect((await mk(429)).retryable).toBe(true);
    expect((await mk(401)).retryable).toBe(false);
  });

  it('sends credentials as a form body, never in the URL', async () => {
    let seen = null;
    global.fetch = vi.fn(async (url, opts) => { seen = { url, opts }; return jsonResponse({}); });
    await request('/api/v1/auth/login', { method: 'POST', form: { username: 'u', password: 'p' } });
    expect(seen.url).toBe('/api/v1/auth/login');
    expect(seen.url).not.toContain('password');
    expect(seen.opts.body).toBe('username=u&password=p');
    expect(seen.opts.headers['Content-Type']).toBe('application/x-www-form-urlencoded');
  });
});

describe('session and cancellation boundaries', () => {
  beforeEach(() => { clearCache(); setToken(null); });
  afterEach(() => vi.restoreAllMocks());
  const response = body => ({ ok:true, status:200, headers:{get:()=> 'application/json'}, json:async()=>body });
  it('does not return a previous account cache after switching accounts', async () => {
    global.fetch = vi.fn(async () => response({ owner:'first' }));
    setToken('first'); await request('/private', { cacheKey:'private' });
    setToken('second'); global.fetch = vi.fn(async()=>response({owner:'second'}));
    expect(await request('/private', { cacheKey:'private' })).toEqual({owner:'second'});
    expect(global.fetch).toHaveBeenCalledTimes(1);
  });
  it('rejects a late response from the signed-out account and cannot refill its cache', async () => {
    let resolve;
    setToken('first');
    global.fetch = vi.fn(()=>new Promise(r => { resolve=r; }));
    const pending=request('/private', { cacheKey:'private' });
    await Promise.resolve();
    setToken(null); resolve(response({secret:true}));
    await expect(pending).rejects.toMatchObject({name:'AbortError'});
    expect(cacheStats().cached).toBe(0);
  });
  it('does not attach new credentials to an old queued request', async () => {
    const resolves=[];
    setToken('first');
    global.fetch=vi.fn(()=>new Promise(r=>resolves.push(r)));
    const calls=Array.from({length:5},(_,i)=>request('/queued/'+i).catch(e=>e));
    await Promise.resolve();
    setToken('second');
    resolves.forEach(r=>r(response({})));
    const results=await Promise.all(calls);
    expect(global.fetch).toHaveBeenCalledTimes(4);
    expect(results.every(e=>e.name==='AbortError')).toBe(true);
    expect(cacheStats().queued).toBe(0);
  });
  it('does not share cancellation between consumers', async () => {
    const controller=new AbortController(); controller.abort();
    global.fetch=vi.fn(async()=>response({ok:true}));
    await expect(request('/same',{signal:controller.signal})).rejects.toMatchObject({name:'AbortError'});
    expect(await request('/same')).toEqual({ok:true});
  });
  it('bounds response time', async () => {
    global.fetch=vi.fn((path,opts)=>new Promise((resolve,reject)=>opts.signal.addEventListener('abort',()=>reject(new DOMException('cancelled','AbortError')))));
    await expect(request('/slow',{timeoutMs:10})).rejects.toThrow('took too long');
    expect(cacheStats().active).toBe(0);
  });
});
