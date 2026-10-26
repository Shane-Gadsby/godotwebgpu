// node web_sweep.mjs <export dir> <out file>: serves the export, opens it headed in system Chrome on the real
// GPU with #sweep, checks the adapter and renderer, collects the SWEEP lines (sweep/web_sweep.gd in the scratch copy).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/package.json');
const { chromium } = require('playwright');
const [dir, outFile] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = http.createServer((req, res) => {
  const p = path.join(dir, decodeURIComponent(req.url.split('?')[0].split('#')[0]) === '/' ? 'index.html' : decodeURIComponent(req.url.split('?')[0]));
  if (!fs.existsSync(p)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[path.extname(p)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  fs.createReadStream(p).pipe(res);
}).listen(0, '127.0.0.1');
await new Promise((r) => server.once('listening', r));
const url = `http://127.0.0.1:${server.address().port}/index.html#sweep`;
const browser = await chromium.launch({ headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist', '--window-size=1600,1000'] });
const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
const lines = [];
let done;
const finished = new Promise((r) => (done = r));
page.on('console', (m) => {
  const t = m.text();
  if (/SWEEP|Forward|WebGPU|adapter|ERROR|NaN/i.test(t)) console.log(t.slice(0, 300));
  if (t.startsWith('SWEEP')) lines.push(t);
  if (t.includes('SWEEP DONE')) done();
});
await page.goto(url.replace('#sweep', ''), { waitUntil: 'domcontentloaded' });
const info = await page.evaluate(async () => {
  const a = await navigator.gpu.requestAdapter();
  return { info: a.info ? { vendor: a.info.vendor, architecture: a.info.architecture, fallback: a.info.isFallbackAdapter } : null, maxSampled: a.limits.maxSampledTexturesPerShaderStage };
});
console.log('ADAPTER', JSON.stringify(info));
await page.goto(url, { waitUntil: 'domcontentloaded' });
await page.evaluate(() => location.reload());
const t = setTimeout(() => done(), 600000);
await finished;
clearTimeout(t);
await page.screenshot({ path: outFile.replace(/\.txt$/, '.png') });
fs.writeFileSync(outFile, `ADAPTER ${JSON.stringify(info)}\n` + lines.join('\n') + '\n');
await browser.close();
server.close();
