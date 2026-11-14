import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

// One html file with the script and the styles inline: the launcher exe carries it and answers the web view from memory, so starting the
// UI reads nothing from disk and makes no extra requests. (The page's content security policy allows only inline script and style.)
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  build: {
    target: 'es2020',
    cssCodeSplit: false,
    assetsInlineLimit: 100_000_000,
    chunkSizeWarningLimit: 1000,
    reportCompressedSize: false,
  },
});
