// capture_web.mjs <export_dir> <out_dir> <prefix> [hash]: headed system Chrome on the real GPU, screenshots per "SHOT".
import { createRequire } from 'module';
import http from 'http'; import fs from 'fs'; import path from 'path';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/resource_lifecycle/node_modules/');
const { chromium } = require('playwright');
const [dir, out, prefix, hash = ''] = process.argv.slice(2);
fs.mkdirSync(out, { recursive: true });
const types = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png' };
const server = http.createServer((req, res) => {
  const f = path.join(dir, decodeURIComponent(req.url.split('?')[0]).replace(/^\/$/, '/index.html'));
  fs.readFile(f, (e, d) => { if (e) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); res.end(d); });
}).listen(0);
const port = server.address().port;
const browser = await chromium.launch({ channel: 'chrome', headless: false,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--use-angle=vulkan', '--ignore-gpu-blocklist', '--window-size=1220,1320'] });
const page = await browser.newPage({ viewport: { width: 1200, height: 1200 } });
let pending = [], done = false, logs = [];
page.on('console', m => { const t = m.text(); logs.push(t);
  const s = t.match(/^SHOT (\S+)/); if (s) pending.push(s[1]); if (t.includes('SHOTS DONE')) done = true; });
await page.goto(`http://localhost:${port}/index.html#${hash}`);
const info = await page.evaluate(async () => { const a = await navigator.gpu.requestAdapter(); return a ? JSON.stringify({ vendor: a.info.vendor, arch: a.info.architecture, desc: a.info.description, fallback: a.info.isFallbackAdapter, maxSampled: a.limits.maxSampledTexturesPerShaderStage }) : 'none'; });
console.log('adapter', info);
const t0 = Date.now(); let n = 0;
while (!done && Date.now() - t0 < 240000) {
  while (pending.length) { const name = pending.shift(); await page.locator('canvas').screenshot({ path: path.join(out, `${prefix}_${name}.png`) }); console.log('saved', name); n++; }
  await new Promise(r => setTimeout(r, 100));
}
while (pending.length) { const name = pending.shift(); await page.locator('canvas').screenshot({ path: path.join(out, `${prefix}_${name}.png`) }); n++; }
console.log(logs.filter(l => /touched|Forward|WebGPU|ERROR|rror/.test(l)).slice(0, 12).join('\n'));
await browser.close(); server.close();
if (n !== 4) { console.log('FAIL: got', n, 'shots'); process.exit(1); }
