import { defineConfig } from 'vitest/config';
export default defineConfig({ test: {
  include: ['src/**/*.test.{js,jsx}'],
  coverage: { provider:'v8', reporter:['text','json-summary','html'],
    include:['src/lib/api.js','src/lib/scheduleModel.js','src/lib/instanceFiles.js'],
    thresholds:{ statements:80, lines:80, functions:80, branches:75 }
  }
} });
