// Minimal screenshot helper: load an export, wait for it to settle, save a PNG.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname, dirname, resolve } from 'path';
import { fileURLToPath } from 'url';
import { mkdtempSync } from 'fs';
import { tmpdir } from 'os';
const __dirname = dirname(fileURLToPath(import.meta.url));
const dir = resolve(process.argv[2]);
const out = resolve(process.argv[3]);
const waitMs = parseInt(process.argv[4] || '12000', 10);
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = createServer((req, res) => {
	const f = join(dir, decodeURIComponent(req.url.split('?')[0]) === '/' ? 'index.html' : decodeURIComponent(req.url.split('?')[0]));
	if (!existsSync(f) || !statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
	res.writeHead(200, { 'Content-Type': MIME[extname(f).toLowerCase()] || 'application/octet-stream', 'Content-Length': statSync(f).size, 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
	res.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const { chromium } = await import(join(__dirname, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
const ctx = await chromium.launchPersistentContext(mkdtempSync(join(tmpdir(), 'shot-')), {
	headless: false, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--gpu-no-context-lost'], viewport: { width: 1280, height: 720 },
});
const page = await ctx.newPage();
await page.goto(`http://127.0.0.1:${server.address().port}/index.html`, { waitUntil: 'domcontentloaded' });
await page.waitForTimeout(waitMs);
await page.screenshot({ path: out });
console.log('wrote', out);
await ctx.close();
server.close();
