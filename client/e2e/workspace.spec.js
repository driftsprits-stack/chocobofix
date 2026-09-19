import { test, expect } from '@playwright/test';
import AxeBuilder from '@axe-core/playwright';
const password = process.env.CHOCOBOFIX_TEST_PASSWORD;
let token, pid;
test.beforeAll(async ({ request }) => {
  const health = await (await request.get('/api/v1/health')).json();
  if(health.needs_bootstrap) expect((await request.post('/api/v1/bootstrap',{form:{username:'review.admin',password}})).ok()).toBeTruthy();
  const login=await request.post('/api/v1/auth/login',{form:{username:'review.admin',password}});
  token=(await login.json()).token;
  const headers={Authorization:`Bearer ${token}`};
  const p=await request.post('/api/v1/projects',{headers,form:{name:'North line / renewal programme'}});
  pid=(await p.json()).id;
  const upload=await request.post(`/api/v1/projects/${pid}/instances/demo`,{headers});
  const instance=await upload.json();
  expect(upload.ok(),JSON.stringify(instance)).toBeTruthy();
  const job=await request.post(`/api/v1/projects/${pid}/jobs`,{headers,form:{instance_id:instance.instance_id,scenario:'all',seconds:30}});
  const j=await job.json();
  expect(job.ok(),JSON.stringify(j)).toBeTruthy();
  await expect.poll(async()=> (await (await request.get(`/api/v1/jobs/${j.job_id}`,{headers})).json()).state,{timeout:40000}).toBe('done');
});
test.beforeEach(async ({ page }) => {
  await page.addInitScript(t=>sessionStorage.setItem('chocobofix.token',t),token);
});
test('projects use the editorial palette and the name field is styled', async ({page})=>{
  await page.goto('/projects');
  await expect(page.getByRole('heading',{name:'projects.'})).toBeVisible();
  await expect(page.getByLabel('Project name')).toBeVisible();
  expect(await page.locator('body').evaluate(el=>getComputedStyle(el).backgroundColor)).toBe('rgb(245, 243, 237)');
  expect(await page.getByLabel('Project name').evaluate(el=>getComputedStyle(el).borderRadius)).toBe('0px');
  await expect(page.getByRole('button',{name:'create project'})).toBeDisabled();
  await page.getByLabel('Project name').fill('South line / night works');
  await expect(page.getByRole('button',{name:'create project'})).toBeEnabled();
  await page.getByRole('button',{name:'create project'}).click();
  await expect(page).toHaveURL(/\/projects\/\d+\/upload/);
});
test('real solved schedules support filters, versions, sorting and keyboard details', async ({page})=>{
  await page.goto(`/schedules?project=${pid}`);
  await expect(page.getByRole('heading',{name:'schedules.'})).toBeVisible();
  await expect(page.locator('.board-table').first()).toBeVisible();
  await page.getByLabel('find work').fill('A001');
  await expect(page.locator('.work-name').first()).toContainText('A001');
  await page.locator('.work-name').first().click();
  const dialog=page.getByRole('dialog'); await expect(dialog).toBeVisible();
  await page.keyboard.press('Tab'); await expect(dialog.getByRole('button',{name:'Close'})).toBeFocused();
  await page.keyboard.press('Shift+Tab'); await expect(dialog.getByLabel('Assign coordinator')).toBeFocused();
  await page.keyboard.press('Escape'); await expect(dialog).not.toBeVisible();
  await expect(page.locator('.work-name').first()).toBeFocused();
  await page.getByRole('button',{name:'clear filters'}).click();
  await expect(page.getByLabel('find work')).toHaveValue('');
  await page.getByLabel('project',{exact:true}).selectOption(String(pid));
  const selector=page.getByRole('combobox',{name:'Plan for North line / renewal programme'});
  await expect(selector.locator('option')).toHaveCount(3);
  await selector.selectOption({index:1});
  await expect(page.locator('.board-table').first()).toBeVisible();
  await page.getByRole('button',{name:'timeline',exact:true}).click();
  await expect(page.locator('.tl-table').first()).toBeVisible();
  await page.getByLabel('group by').selectOption('person');
  await expect(page.locator('.tl-table tbody tr')).toHaveCount(40);
  await page.screenshot({path:'test-results/schedules-desktop.png',fullPage:true});
});
test('mobile supports real work without page overflow', async ({page})=>{
  await page.setViewportSize({width:390,height:844});
  await page.goto(`/schedules?project=${pid}&q=A001`);
  await expect(page.locator('.work-name').first()).toBeVisible();
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth)).toBeTruthy();
  await page.screenshot({path:'test-results/schedules-mobile.png',fullPage:true});
  await page.locator('.work-name').first().click();
  await expect(page.getByRole('dialog')).toBeVisible();
  await page.getByRole('button',{name:'Close'}).click();
});
test('automated accessibility checks on projects, schedules and sign-in',async({page})=>{
  for(const path of ['/projects',`/schedules?project=${pid}&q=A001`,'/signin']) {
    await page.goto(path); await expect(page.locator('h1')).toBeVisible();
    if(path.startsWith('/schedules')) await expect(page.locator('.work-name').first()).toBeVisible();
    const result=await new AxeBuilder({page}).withTags(['wcag2a','wcag2aa','wcag21aa','wcag22aa']).analyze();
    expect(result.violations,JSON.stringify(result.violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)})))).toEqual([]);
  }
});
test('API rejects cross-site writes and does not permit browser caching',async({request})=>{
  const denied=await request.post('/api/v1/projects',{headers:{Authorization:`Bearer ${token}`,Origin:'https://attacker.invalid'},form:{name:'blocked'}});
  expect(denied.status()).toBe(403);
  const response=await request.get('/api/v1/projects',{headers:{Authorization:`Bearer ${token}`}});
  expect(response.headers()['cache-control']).toContain('no-store');
  expect(response.headers()['content-security-policy']).toContain("frame-ancestors 'none'");
});

test('all routes fit small screens and have distinct page metadata', async ({page}) => {
  await page.setViewportSize({width:320,height:740});
  const paths=['/','/overview','/projects',`/projects/${pid}`,`/projects/${pid}/upload`,`/schedules?project=${pid}&q=A001`,`/schedules?project=${pid}&q=A001&view=timeline`,'/settings','/privacy','/cookies','/terms','/missing-page','/signin'];
  for(const path of paths) {
    await page.goto(path); await expect(page.locator('h1')).toBeVisible();
    if(path.startsWith('/schedules')) await expect(page.locator('.proj-head')).toBeVisible();
    expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),path).toBeTruthy();
    const overflowing=await page.locator('main *').evaluateAll(elements=>elements.filter(e=>{const s=getComputedStyle(e);const r=e.getBoundingClientRect();return s.position!=='absolute' && r.width>0 && r.right>innerWidth+2;}).map(e=>e.className));
    expect(overflowing,path).toEqual([]);
    await expect(page).not.toHaveTitle('ChocoboFix');
    await expect(page.locator('meta[name="description"]')).toHaveAttribute('content',/.+/);
  }
});
test('dark theme, mobile navigation and accessibility', async ({page}) => {
  await page.setViewportSize({width:390,height:844}); await page.goto('/settings');
  await page.getByRole('radio',{name:'charcoal',exact:true}).check();
  await expect(page.locator('html')).toHaveAttribute('data-theme','dark');
  await page.getByRole('button',{name:'menu',exact:false}).click();
  await page.getByRole('link',{name:'today',exact:true}).click();
  await expect(page.getByRole('heading',{name:'hello, review.admin.'})).toBeVisible();
  for(const path of ['/settings','/overview',`/schedules?project=${pid}&q=A001&view=timeline`,'/terms']) {
    await page.goto(path); await expect(page.locator('h1')).toBeVisible();
    if(path.startsWith('/schedules')) await expect(page.locator('.tl-table')).toBeVisible();
    const result=await new AxeBuilder({page}).withTags(['wcag2a','wcag2aa','wcag21aa','wcag22aa']).analyze();
    expect(result.violations,JSON.stringify(result.violations.map(v=>({id:v.id,nodes:v.nodes.map(n=>n.target)})))).toEqual([]);
  }
});
