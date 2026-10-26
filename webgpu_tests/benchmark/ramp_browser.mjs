// Serve a load-ramp export (run_ramp.sh) and run it in a headed browser until the scene prints RAMP_ALL_DONE.
//   node ramp_browser.mjs --dir <export dir> [--browser chrome|firefox] [--query "ramps=draws&target_fps=20"]
//                         [--timeout-s 1200] [--capped] [--size 1280x720] [--shots <dir>]
// --shots saves <dir>/<ramp>.png at each ramp's third step, to check every leg draws the same content.
// Chrome is system Chrome with the project's standard flags; uncapped by default (adds --disable-frame-rate-limit
// --disable-gpu-vsync, Firefox layout.frame_rate=0) so frame times near the target are not quantized to vblanks.
// Prints every console line, plus "[BENCHMARK] browser_env ..." naming the WebGPU adapter and WebGL renderer, so a
// swiftshader/llvmpipe run is visible in the log (software=1) and summarize_ramp.py can flag it.
// Playwright: ../scene_smoketest/node_modules, or RAMP_PLAYWRIGHT=<dir containing node_modules/playwright>.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, mkdirSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import { execFileSync } from 'child_process';
import { FIREFOX_WEBGPU_PREFS } from '../scene_smoketest/firefox_prefs.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir', '.'));
const BROWSER = arg('browser', 'chrome');
const QUERY = arg('query', '');
const TIMEOUT_S = +arg('timeout-s', 1200);
const UNCAPPED = !process.argv.includes('--capped');
const [W, H] = arg('size', '1280x720').split('x').map(Number);
const SHOTS = arg('shots', '');

const pwCandidates = [process.env.RAMP_PLAYWRIGHT, join(HERE, '..', 'scene_smoketest')].filter(Boolean)
	.map((d) => join(d, 'node_modules', 'playwright', 'index.mjs'));
const pwPath = pwCandidates.find((p) => existsSync(p));
if (!pwPath) {
	console.log(`FAILED: playwright not found (tried ${pwCandidates.join(', ')}); run npm install in scene_smoketest or set RAMP_PLAYWRIGHT`);
	process.exit(2);
}
const pw = await import(pwPath);

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.bin': 'application/octet-stream', '.json': 'application/json', '.svg': 'image/svg+xml' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]);
	const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
	r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));

// Headed always: headless gets no real adapter (CLAUDE.md, TASKS.md §3 trap 43).
const browser = BROWSER === 'firefox'
	? await pw.firefox.launch({ headless: false, firefoxUserPrefs: { ...FIREFOX_WEBGPU_PREFS, ...(UNCAPPED ? { 'layout.frame_rate': 0 } : {}) } })
	: await pw.chromium.launch({
		headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist',
			...(UNCAPPED ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : [])],
	});
const page = await browser.newPage({ viewport: { width: W, height: H }, deviceScaleFactor: 1 });

let done;
const finished = new Promise((r) => { done = r; });
page.on('console', (m) => {
	const t = m.text();
	console.log(t);
	if (t.includes('RAMP_ALL_DONE')) done(true);
	const sm = SHOTS && /^\[RAMP\] ramp=(\w+) step=2 phase=grow n=/.exec(t);
	if (sm) {
		mkdirSync(SHOTS, { recursive: true });
		page.screenshot({ path: join(SHOTS, `${sm[1]}.png`) }).catch((e) => console.log('[ramp_browser] screenshot failed: ' + e.message));
	}
});
page.on('pageerror', (e) => console.log('[PAGE-ERROR] ' + e.message));
page.on('crash', () => { console.log('[PAGE-CRASH]'); done(false); });
await page.addInitScript(() => {
	if (typeof GPUAdapter === 'undefined') return;
	const rd = GPUAdapter.prototype.requestDevice;
	GPUAdapter.prototype.requestDevice = function (...a) {
		const ai = this.info || {};
		window.__rampAdapter = `${ai.vendor || '?'}/${ai.architecture || '?'}/${ai.description || ''}`.replace(/\s+/g, '_');
		return rd.apply(this, a);
	};
});
const t0 = Date.now();
await page.goto(`http://127.0.0.1:${server.address().port}/${QUERY ? '?' + QUERY.replace(/^\?/, '') : ''}`);

// Report the adapter once the engine is up (the WebGPU one is captured when the engine requests its device).
setTimeout(async () => {
	try {
		const env = await page.evaluate(() => {
			let gl = '';
			try {
				const c = document.createElement('canvas').getContext('webgl2');
				const ext = c && c.getExtension('WEBGL_debug_renderer_info');
				gl = c ? (ext ? c.getParameter(ext.UNMASKED_RENDERER_WEBGL) : c.getParameter(c.RENDERER)) : 'none';
			} catch (e) { gl = 'error'; }
			const cv = document.querySelector('canvas');
			return { webgpu: window.__rampAdapter || 'none', webgl: String(gl).replace(/\s+/g, '_'), canvas: cv ? `${cv.width}x${cv.height}` : '?', dpr: window.devicePixelRatio };
		});
		const sw = /swiftshader|llvmpipe|software|lavapipe/i.test(env.webgpu + env.webgl) ? 1 : 0;
		let gpuProcs = 0;
		try {
			gpuProcs = execFileSync('nvidia-smi', { encoding: 'utf8' }).split('\n').filter((l) => new RegExp(BROWSER === 'firefox' ? 'firefox' : 'chrome', 'i').test(l)).length;
		} catch { gpuProcs = -1; }
		console.log(`[BENCHMARK] browser_env browser=${BROWSER} uncapped=${UNCAPPED ? 1 : 0} canvas=${env.canvas} dpr=${env.dpr} software=${sw} nvidia_procs=${gpuProcs} webgpu_adapter=${env.webgpu} webgl_renderer=${env.webgl}`);
		if (sw) console.log('[BENCHMARK] WARNING: software adapter -- this run does not measure the GPU');
	} catch (e) {
		console.log('[BENCHMARK] browser_env error ' + e.message);
	}
}, 8000);

const ok = await Promise.race([finished, new Promise((r) => setTimeout(() => r(false), TIMEOUT_S * 1000))]);
console.log(ok ? `[ramp_browser] done in ${((Date.now() - t0) / 1000).toFixed(0)} s` : `[ramp_browser] FAILED: no RAMP_ALL_DONE within ${TIMEOUT_S} s`);
await new Promise((r) => setTimeout(r, 500));
await browser.close();
server.close();
process.exit(ok ? 0 : 1);
