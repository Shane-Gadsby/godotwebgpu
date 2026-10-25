// Frame time of a WebGPU export in Chrome or Firefox, for browsers without 'timestamp-query' (work task 11).
//
//   node frame_time.mjs --dir <export dir> [--browser chrome|firefox] [--uncapped] [--settle 12000] [--window 10000]
//                       [--size 1280x720]
//
// gpu_time.mjs needs timestamp queries, which Firefox does not expose, so this measures what every browser has: the
// interval between requestAnimationFrame callbacks (Emscripten's main loop runs one engine frame per callback) over a
// window after a settle. Uncapped is --disable-frame-rate-limit/--disable-gpu-vsync in Chrome and layout.frame_rate=0
// (Firefox's ASAP mode) in Firefox. Uncapped, a GPU-bound scene's frame time follows its GPU time, so two exports that
// differ only in their WGSL compare by it; capped at vsync, both read 16.7 ms and say nothing.
// Prints fps, mean/median/p95 ms per frame, the adapter (Forward+ needs maxSampledTexturesPerShaderStage >= 48), the
// renderer banner and the shader sources (baked/translated), so a log that measured the wrong thing shows it.
// WEBGPU_AUDIT=<file.jsonl> adds the shader/pipeline audit (scene_smoketest/webgpu_audit.js); leave it off for timing.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import { execFileSync } from 'child_process';
import { attachAudit } from '../scene_smoketest/webgpu_audit.mjs';
import { FIREFOX_WEBGPU_PREFS } from '../scene_smoketest/firefox_prefs.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const BROWSER = arg('browser', 'chrome');
const SETTLE = +arg('settle', 12000);
const WINDOW = +arg('window', 10000);
const UNCAPPED = process.argv.includes('--uncapped');
const [W, H] = arg('size', '1280x720').split('x').map(Number);
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.bin': 'application/octet-stream' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]);
	const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
	r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const pw = await import(join(HERE, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
// Headed always: headless gets no real adapter (CLAUDE.md, TASKS.md §3 trap 43).
const browser = BROWSER === 'firefox'
	? await pw.firefox.launch({
		headless: false,
		firefoxUserPrefs: { ...FIREFOX_WEBGPU_PREFS, ...(UNCAPPED ? { 'layout.frame_rate': 0 } : {}) },
	})
	: await pw.chromium.launch({
		headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist',
			...(UNCAPPED ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : [])],
	});
const page = await browser.newPage({ viewport: { width: W, height: H } });
await attachAudit(page, 'frame_time');
let banner = '';
let bannerAt = 0;
const t0 = Date.now();
let errors = 0;
const firstErrors = [];
page.on('console', (m) => {
	const t = m.text();
	if (t.startsWith('[frame_time]')) console.log(t);
	if (!banner && /WebGPU .* - Forward/.test(t)) { banner = t.trim(); bannerAt = Date.now() - t0; }
	if (m.type() === 'error' && !/AudioContext|float32-blendable|Source map/i.test(t)) {
		errors++;
		if (firstErrors.length < 3) firstErrors.push(t.slice(0, 200));
	}
});
page.on('pageerror', (e) => { errors++; if (firstErrors.length < 3) firstErrors.push(e.message.slice(0, 200)); });
await page.addInitScript(() => {
	const F = window.__ft = { on: false, t: [] };
	const raf = window.requestAnimationFrame.bind(window);
	window.requestAnimationFrame = (cb) => raf((ts) => { if (F.on) F.t.push(performance.now()); cb(ts); });
	const rd = GPUAdapter.prototype.requestDevice;
	GPUAdapter.prototype.requestDevice = function (...a) {
		const ai = this.info || {};
		console.log(`[frame_time] adapter ${ai.vendor || '?'}/${ai.architecture || '?'} ${ai.description || ''}, maxSampledTexturesPerShaderStage ${this.limits.maxSampledTexturesPerShaderStage}, timestamp-query ${this.features.has('timestamp-query')}`);
		return rd.apply(this, a);
	};
});
await page.goto(`http://127.0.0.1:${server.address().port}/`);
await new Promise((r) => setTimeout(r, SETTLE));
await page.evaluate(() => { window.__ft.t = []; window.__ft.on = true; });
await new Promise((r) => setTimeout(r, WINDOW / 2));
// Which GPU the browser is on: Firefox hides GPUAdapterInfo, so the proof that a number is real-GPU is its process in
// nvidia-smi's table (graphics and compute) halfway through the window.
let gpuProcs = '';
try {
	gpuProcs = execFileSync('nvidia-smi', { encoding: 'utf8' }).split('\n').filter((l) => new RegExp(BROWSER === 'firefox' ? 'firefox' : 'chrome', 'i').test(l)).map((l) => l.replace(/\s+/g, ' ').trim()).join(' | ');
} catch {
	gpuProcs = 'nvidia-smi unavailable';
}
await new Promise((r) => setTimeout(r, WINDOW / 2));
const r = await page.evaluate(() => {
	const F = window.__ft;
	F.on = false;
	const sh = window.godotWebGPUShaderStats;
	return { t: F.t, shaders: sh ? { baked: sh.baked, precompiled: sh.precompiled, translated: sh.translated } : null };
});
const d = [];
for (let i = 1; i < r.t.length; i++) d.push(r.t[i] - r.t[i - 1]);
d.sort((a, b) => a - b);
if (d.length < 10) {
	console.log(`FAILED: only ${r.t.length} frames in ${WINDOW} ms (engine not running?)`);
	process.exitCode = 1;
} else {
	const span = r.t[r.t.length - 1] - r.t[0];
	const q = (p) => d[Math.min(d.length - 1, Math.floor(d.length * p))];
	console.log(`browser ${BROWSER}${UNCAPPED ? ' uncapped' : ''}, ${W}x${H}, settle ${SETTLE} ms, window ${WINDOW} ms`);
	console.log(`renderer: ${banner || '(no banner seen)'} (banner ${bannerAt} ms after navigation)`);
	console.log(`frames ${d.length} over ${span.toFixed(0)} ms -> ${(1000 * d.length / span).toFixed(1)} fps, mean ${(span / d.length).toFixed(3)} ms/frame, median ${q(0.5).toFixed(3)}, p95 ${q(0.95).toFixed(3)}, max ${d[d.length - 1].toFixed(1)}`);
}
console.log(`shader sources: ${JSON.stringify(r.shaders)}`);
console.log(`nvidia-smi: ${gpuProcs || '(browser not in the GPU process table)'}`);
console.log(`console errors: ${errors}${firstErrors.length ? ' first: ' + firstErrors.join(' | ') : ''}`);
await browser.close();
server.close();
