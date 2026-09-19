import { beforeEach, afterEach, expect, it, vi } from 'vitest';
import { api, clearCache, setToken, request, getToken } from '../api.js';
beforeEach(()=>{ setToken('contract-test'); clearCache(); global.fetch=vi.fn(async()=>({ok:true,status:200,headers:{get:()=> 'application/json'},json:async()=>({user:{id:1}})})); });
afterEach(()=>{setToken(null);vi.restoreAllMocks();});
// Published service routes. Guard their methods and ensure object-scoped IDs
// remain on requests after client refactors; protected reads must carry auth.
it.each([
  ['health',[],'GET','/health'],['me',[],'GET','/auth/me'],
  ['bootstrap',['name','password'],'POST','/bootstrap'],['login',['name','password'],'POST','/auth/login'],['logout',[],'POST','/auth/logout'],
  ['projects',[],'GET','/projects'],['createProject',['renewal'],'POST','/projects'],
  ['instances',[7],'GET','/projects/7/instances'],['loadDemo',[7],'POST','/projects/7/instances/demo'],
  ['uploadInstance',[7,new FormData()],'POST','/projects/7/instances'],
  ['instanceDetail',[8],'GET','/instances/8/detail'],['createJob',[7,8,'A',30],'POST','/projects/7/jobs'],
  ['job',['abc'],'GET','/jobs/abc'],['jobLog',['abc'],'GET','/jobs/abc/log'],['cancelJob',['abc'],'POST','/jobs/abc/cancel'],
  ['versions',[7],'GET','/projects/7/versions'],['validation',[9],'GET','/versions/9/validation'],
  ['versionFile',[9,'RESULTS.csv'],'GET','/versions/9/files/RESULTS.csv'],['approve',[9],'POST','/versions/9/approve'],
  ['audit',[7],'GET','/projects/7/audit'],['assignments',[7,8],'GET','/projects/7/assignments?instance_id=8'],
  ['assign',[7,{instance_id:8,activity_id:'A001',coordinator_id:1}],'POST','/projects/7/assignments'],
  ['users',[],'GET','/users'],['uploadPhoto',[new FormData()],'POST','/profile/photo'],['removePhoto',[],'POST','/profile/photo'],
  ['repair',[8,{location:'SEC:ALP:S01_S02:EB',week:3,supply:1}],'POST','/instances/8/repair'],
])('%s follows its server contract',async(name,args,method,path)=>{
  await api[name](...args);
  const [url,options]=global.fetch.mock.calls[0];
  expect(url).toBe('/api/v1'+path);expect(options.method).toBe(method);
  expect(options.headers.Authorization).toBe('Bearer contract-test');
  expect(options.cache).toBe('no-store');
  if(method==='GET') expect(options.body).toBeUndefined();
});
it('restores both wrapped and legacy user responses',async()=>{
  expect(await api.me()).toEqual({id:1});
  global.fetch=vi.fn(async()=>({ok:true,status:200,headers:{get:()=> 'application/json'},json:async()=>({id:2})}));
  expect(await api.me()).toEqual({id:2}); expect(getToken()).toBe('contract-test');
});
it('parses CSV exports as text',async()=>{
  global.fetch=vi.fn(async()=>({ok:true,status:200,headers:{get:()=> 'text/csv'},text:async()=> 'activity_id,week\nA1,2'}));
  expect(await api.versionFile(1,'SCHEDULE_ACCESS.csv')).toContain('A1,2');
});
it('bounds immutable cache and lets a prefix invalidate it',async()=>{
  for(let n=0;n<70;n++) await request('/file/'+n,{cacheKey:'file:'+n});
  const {cacheStats}=await import('../api.js');expect(cacheStats().cached).toBe(64);
  clearCache('file:');expect(cacheStats().cached).toBe(0);
});
