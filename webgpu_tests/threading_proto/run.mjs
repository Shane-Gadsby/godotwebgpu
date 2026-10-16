// node run.mjs <query>   e.g. "logic=4&render=4"
import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath } from 'url';
const HERE = dirname(fileURLToPath(import.meta.url)); const Q = process.argv[2] || '';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const server = createServer((q, r) => { const p = q.url.split('?')[0]; const f = join(HERE, p === '/' ? 'index.html' : p);
	if (!existsSync(f)) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f)); });
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const { chromium } = await import(join(HERE, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
const browser = await chromium.launch({ headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'] });
const page = await browser.newPage();
page.on('console', (m) => console.log(m.text()));
page.on('pageerror', (e) => console.log('PAGEERROR ' + e.message));
await page.goto(`http://127.0.0.1:${server.address().port}/?${Q}`);
await new Promise((r) => setTimeout(r, 9000));
await page.screenshot({ path: join(HERE, 'shot1.png') }); await new Promise((r) => setTimeout(r, 500)); await page.screenshot({ path: join(HERE, 'shot2.png') });
await browser.close(); server.close();
