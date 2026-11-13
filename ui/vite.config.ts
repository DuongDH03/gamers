import { defineConfig, type Plugin } from 'vitest/config';
import react from '@vitejs/plugin-react';

// The built page must load both from a virtual host (WebView2's
// SetVirtualHostNameToFolderMapping) and straight from a file:// URL.
// Chromium refuses ES module scripts and CORS-mode stylesheets from file://,
// so the build is one classic (IIFE) script loaded with `defer`, and the
// `type="module"` / `crossorigin` attributes Vite writes are removed.
function classicScriptForFileUrls(): Plugin {
  return {
    name: 'scraps-classic-script',
    apply: 'build',
    enforce: 'post',
    transformIndexHtml: {
      order: 'post',
      handler(html) {
        return html
          .replace(/<script type="module" crossorigin src=/g, '<script defer src=')
          .replace(/<link rel="stylesheet" crossorigin href=/g, '<link rel="stylesheet" href=');
      },
    },
  };
}

export default defineConfig({
  base: './',
  plugins: [react(), classicScriptForFileUrls()],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: 'es2022',
    modulePreload: false,
    // One stylesheet linked from index.html (with IIFE output Vite would
    // otherwise inject the CSS from JavaScript).
    cssCodeSplit: false,
    sourcemap: false,
    rollupOptions: {
      output: {
        format: 'iife',
        inlineDynamicImports: true,
      },
    },
  },
  test: {
    environment: 'jsdom',
    setupFiles: ['./src/test/setup.ts'],
    include: ['src/**/*.test.{ts,tsx}'],
    restoreMocks: true,
  },
});
