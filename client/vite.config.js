import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// Output lands where the native service's --web flag points, so the hosted
// interface and the local one are the same bytes.
export default defineConfig({
  plugins: [react()],
  build: {
    outDir: '../web-dist',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        // Route and visualisation code splits away from the public entry so the
        // landing page stays inside the brief's 180KB gzip budget.
        manualChunks(id) {
          if (id.includes('node_modules/react-router')) return 'router';
          if (id.includes('node_modules')) return 'react';
        },
      },
    },
  },
  server: {
    port: 5173,
    proxy: { '/api': { target: 'http://127.0.0.1:8080', changeOrigin: true } },
  },
});
