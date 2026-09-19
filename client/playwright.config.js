import { randomBytes } from 'node:crypto';
import { defineConfig } from '@playwright/test';
process.env.CHOCOBOFIX_TEST_PASSWORD ||= randomBytes(18).toString('hex');
export default defineConfig({
  testDir: './e2e', fullyParallel: false, workers: 1, timeout: 45000,
  reporter: [['list'], ['html', { open: 'never' }]],
  use: { baseURL: 'http://127.0.0.1:8189', viewport: { width: 1440, height: 1000 }, trace: 'retain-on-failure' },
  webServer: { command: 'node e2e/server.mjs', url: 'http://127.0.0.1:8189/api/v1/health', reuseExistingServer: false, timeout: 30000 },
});
