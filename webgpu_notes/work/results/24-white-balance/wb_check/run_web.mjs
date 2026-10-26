// Serve a wb_check web export and print its [WB] lines (and the adapter) from headed system Chrome on the real GPU.
import { createServer } from 'http';
import { readFileSync, existsSync, writeFileSync } from 'fs';
import { join, extname } from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu-24/webgpu_tests/forward_plus/package.json');
const pw = require('playwright');
const dir = process.argv[2];
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
const page = await browser.newPage({ viewport: { width: 320, height: 180 } });
const lines = []; const shots = [];
let done;
const finished = new Promise((r) => (done = r));
page.on('console', (m) => { const t = m.text(); if (t.startsWith('[WBPNG] ')) { const [, name, b64] = t.split(' '); if (process.argv[4]) writeFileSync(join(process.argv[4], name), Buffer.from(b64, 'base64')); return; } if (t.startsWith('[WB] READY ')) { const name = t.split(' ')[2]; shots.push(page.locator('canvas').screenshot({ path: join(process.argv[4], name) })); } if (/\[WB\]|ERROR|rror/.test(t)) { console.log(t); lines.push(t); } else if (process.env.ALL) console.log('  console:', t.slice(0, 300)); if (t.includes('[WB] DONE')) done(); });
await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
const info = await page.evaluate(async () => { const a = await navigator.gpu.requestAdapter(); return a ? [a.info.vendor, a.info.architecture, a.info.description, 'fallback=' + a.info.isFallbackAdapter].join(' ') : 'none'; });
console.log('adapter', info);
await Promise.race([finished, new Promise((_, rej) => setTimeout(() => rej(new Error('timeout: no [WB] DONE in 90 s')), 90000))]);
await Promise.all(shots);
if (process.argv[3]) writeFileSync(process.argv[3], lines.join('\n') + '\n');
await browser.close(); server.close();
