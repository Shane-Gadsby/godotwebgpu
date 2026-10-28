// web_capture.mjs <export_dir> <out.png> <hash args> [WxH]: headed system Chrome on the real GPU, saves the frame the
// game prints as [MBPNG] (main.gd), prints the adapter and its maxSampledTexturesPerShaderStage.
import { createServer } from 'http';
import { readFileSync, existsSync, writeFileSync } from 'fs';
import { join, extname } from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/package.json');
const pw = require('playwright');
const [dir, out, hash, size = '1280x720'] = process.argv.slice(2);
const [w, h] = size.split('x').map(Number);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = createServer((req, res) => {
  const p = join(dir, decodeURIComponent(req.url.split('?')[0]).replace(/^\/$/, '/index.html'));
  if (!existsSync(p)) { res.writeHead(404); res.end(); return; }
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  res.setHeader('Content-Type', types[extname(p)] || 'application/octet-stream');
  res.end(readFileSync(p));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const browser = await pw.chromium.launch({ headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'] });
const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
let done; const finished = new Promise((r) => (done = r));
let errors = 0; let statusHidden = false; let capturing = false;
page.on('console', (m) => {
  const t = m.text();
  if (t.startsWith('[MBPNG] ')) { writeFileSync(out, Buffer.from(t.slice(8).trim(), 'base64')); return; }
  if (/ERROR|rror|WARNING/.test(t)) { errors++; console.log('  console:', t.slice(0, 300)); }
  // The game says READY on every frame worth capturing; take the first one after the loading overlay is gone.
  if (t.startsWith('[MB] READY') && statusHidden && !capturing) { capturing = true; console.log(t); page.locator('canvas').screenshot({ path: out }).then(() => done()); }
});
await page.goto(`http://127.0.0.1:${server.address().port}/index.html#${hash}`);
const info = await page.evaluate(async () => { const a = await navigator.gpu.requestAdapter(); return a ? [a.info.vendor, a.info.architecture, 'fallback=' + a.info.isFallbackAdapter, 'maxSampledTex=' + a.limits.maxSampledTexturesPerShaderStage].join(' ') : 'none'; });
console.log('adapter', info);
await page.waitForFunction(() => { const s = document.getElementById('status'); return !s || getComputedStyle(s).display === 'none' || getComputedStyle(s).visibility === 'hidden'; }, null, { timeout: 110000 });
await new Promise((r) => setTimeout(r, 1500)); // Let the first frames after the overlay settle.
statusHidden = true;
await Promise.race([finished, new Promise((_, rej) => setTimeout(() => rej(new Error('timeout: no [MB] READY in 120 s')), 120000))]);
console.log('errors/warnings in console:', errors);
await browser.close().catch(() => {}); server.close(); process.exit(0);
