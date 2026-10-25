import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname } from 'path';
const dir = process.argv[2]; const real = process.argv[3] === 'real'; const TIMEOUT = 240000;
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = createServer((req, res) => {
  let p = join(dir, req.url.split('?')[0]); if (p.endsWith('/')) p += 'index.html';
  if (!existsSync(p)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[extname(p)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  res.end(readFileSync(p));
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/`;
const { chromium } = await import('playwright');
const args = real ? ['--enable-unsafe-webgpu','--enable-features=Vulkan','--use-vulkan','--use-angle=vulkan','--use-vulkan=native','--ignore-gpu-blocklist','--no-sandbox']
  : ['--enable-unsafe-webgpu','--enable-features=Vulkan,UseSkiaRenderer','--use-angle=swiftshader','--enable-gpu','--no-sandbox'];
const browser = await chromium.launch({ headless: true, args });
const page = await browser.newPage();
let fin = false, pass = false; const errs = [];
page.on('console', m => { const t = m.text();
  if (/ShaderCoverage\] PASS/.test(t)) { fin = pass = true; }
  if (/ShaderCoverage\] FAIL/.test(t)) { fin = true; }
  if (m.type() === 'error' || /SPIR-V.WGSL failed/.test(t)) errs.push(t.slice(0, 300));
  if (process.env.VERBOSE) console.log(m.type(), t.slice(0, 200)); });
page.on('pageerror', e => errs.push('PAGEERROR ' + e.message.slice(0, 300)));
await page.goto(url);
const t0 = Date.now();
while (!fin && Date.now() - t0 < TIMEOUT) await new Promise(r => setTimeout(r, 1000));
const adapter = await page.evaluate(async () => { try { const a = await navigator.gpu.requestAdapter(); const i = a.info || {}; return `${i.vendor}/${i.architecture}/${i.description} fallback=${a.isFallbackAdapter}`; } catch (e) { return 'n/a ' + e; } });
const st = await page.evaluate(() => window.godotWebGPUShaderStats || null);
console.log('adapter', adapter);
console.log('finished', fin, 'pass', pass, 'seconds', Math.round((Date.now() - t0) / 1000));
console.log('shaderStats', JSON.stringify(st));
console.log('errors', errs.length); errs.slice(0, 8).forEach(e => console.log('  ', e));
await browser.close(); server.close();
