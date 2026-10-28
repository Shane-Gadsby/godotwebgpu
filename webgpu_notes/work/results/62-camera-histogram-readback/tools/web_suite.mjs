// web_suite.mjs <export_dir> <hash> <timeout_s>: headed system Chrome on the real GPU; asserts the adapter, prints the
// game's RESULT lines and every console error/warning, exits when "done" (suite) or after timeout.
import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname } from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/package.json');
const pw = require('playwright');
const [dir, hash = 'suite', tmo = '180'] = process.argv.slice(2);
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
const page = await browser.newPage({ viewport: { width: 640, height: 360 }, deviceScaleFactor: 1 });
let errors = 0; let done; const finished = new Promise((r) => (done = r));
page.on('console', (m) => {
  const t = m.text();
  if (t.startsWith('RESULT ')) { console.log(t); if (t.includes('"test":"done"') || t.includes('"perf_mode"')) done(); return; }
  if (m.type() === 'error' || m.type() === 'warning' || /ERROR|WARNING/.test(t)) { errors++; console.log('  console[' + m.type() + ']:', t.slice(0, 400)); }
});
page.on('pageerror', (e) => { errors++; console.log('  pageerror:', String(e).slice(0, 300)); });
await page.goto(`http://127.0.0.1:${server.address().port}/index.html#${hash}`);
const a = await page.evaluate(async () => { const a = await navigator.gpu.requestAdapter(); return a ? { vendor: a.info.vendor, arch: a.info.architecture, fallback: a.info.isFallbackAdapter, maxTex: a.limits.maxSampledTexturesPerShaderStage } : null; });
console.log('adapter', JSON.stringify(a));
if (!a || a.vendor !== 'nvidia' || a.arch !== 'lovelace' || a.fallback || a.maxTex !== 48) { console.log('ADAPTER ASSERT FAILED'); await browser.close(); server.close(); process.exit(2); }
const ok = await Promise.race([finished.then(() => true), new Promise((r) => setTimeout(() => r(false), Number(tmo) * 1000))]);
console.log(ok ? 'finished' : 'TIMEOUT', 'console errors/warnings:', errors);
await browser.close().catch(() => {}); server.close(); process.exit(ok ? 0 : 1);
