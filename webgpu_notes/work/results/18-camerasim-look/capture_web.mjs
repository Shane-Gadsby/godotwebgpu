// Task 18: run a cameraSim web export (with the t18_capture.gd autoload) in headed system Chrome on the real GPU,
// save the PNGs the autoload hands over (JavaScriptBridge.download_buffer), plus console, adapter and shader stats.
//   node capture_web.mjs <export_dir> <out_dir> [timeout_ms]
// Fails (exit 1) if the adapter is a fallback/software one, if a view is missing, or on timeout.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, mkdirSync, writeFileSync } from 'fs';
import { join, extname, resolve } from 'path';
const DIR = resolve(process.argv[2]);
const OUT = resolve(process.argv[3]);
const TIMEOUT = +(process.argv[4] || 240000);
const VIEWS = ['default', 'face_200mm', 'dof_f1_4', 'hair_side_200mm', 'face_200mm_lights'];
mkdirSync(OUT, { recursive: true });
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const { chromium } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/scene_smoketest/node_modules/playwright/index.mjs');
const browser = await chromium.launch({
	headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
});
const ctx = await browser.newContext({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1, acceptDownloads: true });
const page = await ctx.newPage();
await page.addInitScript(() => {
	const ra = GPU.prototype.requestAdapter;
	GPU.prototype.requestAdapter = async function (...a) {
		const ad = await ra.apply(this, a);
		if (ad) {
			const i = ad.info || {};
			console.log(`[t18-adapter] vendor=${i.vendor} arch=${i.architecture} device=${i.device} desc=${i.description} fallback=${i.isFallbackAdapter} maxSampledTex=${ad.limits.maxSampledTexturesPerShaderStage}`);
		}
		return ad;
	};
});
const log = [];
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(2);
let readyAt = null, firstCaptureAt = null, done = false;
page.on('console', (m) => {
	const line = `[${stamp()}s] [${m.type()}] ${m.text()}`;
	log.push(line);
	if (m.text().startsWith('[t18')) console.log(line);
	if (m.text().startsWith('[t18] ready') && readyAt === null) readyAt = stamp();
	if (m.text().startsWith('[t18] done')) done = true;
});
page.on('pageerror', (e) => log.push(`[${stamp()}s] [pageerror] ${e.message}`));
const saved = [];
page.on('download', async (d) => {
	const name = d.suggestedFilename();
	if (firstCaptureAt === null) firstCaptureAt = stamp();
	await d.saveAs(join(OUT, name));
	saved.push(name);
	console.log(`[${stamp()}s] saved ${name}`);
});
await page.goto(`http://127.0.0.1:${server.address().port}/`);
const until = Date.now() + TIMEOUT;
while (!(done && saved.length >= VIEWS.length) && Date.now() < until) await new Promise((r) => setTimeout(r, 500));
await new Promise((r) => setTimeout(r, 1000));
await page.screenshot({ path: join(OUT, 'page_last.png') });
const stats = await page.evaluate(() => window.godotWebGPUShaderStats || null);
const fps = await page.evaluate(() => new Promise((res) => { let n = 0; const s = performance.now(); const f = () => { n++; if (performance.now() - s < 3000) requestAnimationFrame(f); else res(n / ((performance.now() - s) / 1000)); }; requestAnimationFrame(f); }));
const errors = log.filter((l) => /\[(error|warning|pageerror)\]/.test(l));
const summary = { readyAt_s: readyAt, firstCaptureAt_s: firstCaptureAt, saved, stats: stats && { baked: stats.baked, precompiled: stats.precompiled, cached: stats.cached, translated: stats.translated, specialized: stats.specialized, translatedShaders: stats.translatedShaders }, rafPerSec: fps, errorCount: errors.length };
writeFileSync(join(OUT, 'console.log'), log.join('\n') + '\n');
writeFileSync(join(OUT, 'summary.json'), JSON.stringify(summary, null, 1));
console.log(JSON.stringify(summary, null, 1));
console.log(`errors/warnings (${errors.length}):\n` + [...new Set(errors.map((l) => l.replace(/^\[[\d.]+s\] /, '')))].slice(0, 30).join('\n'));
await browser.close();
server.close();
const adapterOk = log.some((l) => l.includes('[t18-adapter]') && l.includes('fallback=false') && /nvidia/i.test(l));
if (!adapterOk) { console.log('FAIL: adapter not the real NVIDIA GPU'); process.exit(1); }
const missing = VIEWS.filter((v) => !saved.includes(`${v}.png`));
if (missing.length) { console.log(`FAIL: missing views ${missing.join(',')}`); process.exit(1); }
