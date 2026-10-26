// Serve a load-ramp export (run_ramp.sh) and run each ramp in its own headed browser.
//   node ramp_browser.mjs --dir <export dir> [--browser chrome|firefox] [--ramps draws,fill,..] [--target-fps 20]
//                         [--max-s 180] [--mem-mb 1400] [--stall-s 90] [--capped] [--no-pace] [--size 1280x720]
//                         [--shots <dir>]
// One browser per ramp: a ramp that loses the device, runs out of heap or leaves the GPU process with a backlog
// cannot poison the next one. Watchdog per ramp: no "[RAMP]" line for --stall-s s, a page error, a page crash, a lost
// WebGPU device, an out-of-memory error, a failed screenshot or the ramp's time budget -> the browser is closed, the
// log gets "[BENCHMARK] ramp_failed ramp=<r> reason=<why>" (summarize_ramp.py reports the last N measured above the
// target, marked failed) and the next ramp starts. Exit 0 if every ramp printed a result, 1 otherwise.
//
// Frame pacing (WebGPU, on unless --no-pace): a frame may start only once the GPU has finished the frame before the
// previous one (GPUQueue.onSubmittedWorkDone), i.e. at most 2 frames in flight, as native Vulkan has. Without it,
// Chrome with the frame-rate limit lifted lets the page queue GPU work faster than the GPU completes it, so the frame
// time reads too fast while the backlog grows, and the backlog then slows every later measurement (2026-10-26: fill
// "carried" 2x native's layers, then every window read 150 ms whatever N was). WebGL is throttled by the browser.
//
// Chrome is system Chrome with the project's standard flags, uncapped by default (--disable-frame-rate-limit
// --disable-gpu-vsync; Firefox layout.frame_rate=0) so frame times near the target are not quantized to vblanks.
// "[BENCHMARK] browser_env ..." names the WebGPU adapter and WebGL renderer (software=1 for swiftshader/llvmpipe).
// Playwright: ../scene_smoketest/node_modules, or RAMP_PLAYWRIGHT=<dir containing node_modules/playwright>.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, mkdirSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import { FIREFOX_WEBGPU_PREFS } from '../scene_smoketest/firefox_prefs.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir', '.'));
const BROWSER = arg('browser', 'chrome');
const RAMPS = arg('ramps', 'draws,nodes,fill,lit,particles').split(',').filter(Boolean);
const TARGET = arg('target-fps', '20');
const MAX_S = +arg('max-s', 180);
const MEM_MB = arg('mem-mb', '');
const STALL_S = +arg('stall-s', 90);
const UNCAPPED = !process.argv.includes('--capped');
const PACE = !process.argv.includes('--no-pace');
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

function launch() {
	// Headed always: headless gets no real adapter (CLAUDE.md, TASKS.md §3 trap 43).
	return BROWSER === 'firefox'
		? pw.firefox.launch({ headless: false, firefoxUserPrefs: { ...FIREFOX_WEBGPU_PREFS, ...(UNCAPPED ? { 'layout.frame_rate': 0 } : {}) } })
		: pw.chromium.launch({
			headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
			args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist',
				...(UNCAPPED ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : [])],
		});
}

// Runs in the page before the engine: wasm heap size for ramp.gd's memory budget (release templates do not track
// static memory, so OS.get_static_memory_usage() reads 0 on the web), adapter capture, and WebGPU frame pacing.
function pageInit(pace) {
	const grab = (res) => {
		const ex = (res && (res.instance || res).exports) || {};
		for (const k in ex) if (ex[k] instanceof WebAssembly.Memory) window.__rampMemory = ex[k];
		return res;
	};
	const inst = WebAssembly.instantiate;
	WebAssembly.instantiate = (...a) => inst.apply(WebAssembly, a).then(grab);
	if (WebAssembly.instantiateStreaming) {
		const st = WebAssembly.instantiateStreaming;
		WebAssembly.instantiateStreaming = (...a) => st.apply(WebAssembly, a).then(grab);
	}
	window.__rampHeapBytes = () => (window.__rampMemory ? window.__rampMemory.buffer.byteLength : 0);
	if (typeof GPUAdapter === 'undefined') return;
	const rd = GPUAdapter.prototype.requestDevice;
	GPUAdapter.prototype.requestDevice = function (...a) {
		const ai = this.info || {};
		window.__rampAdapter = `${ai.vendor || '?'}/${ai.architecture || '?'}/${ai.description || ''}`.replace(/\s+/g, '_');
		return rd.apply(this, a);
	};
	if (!pace) return;
	let cur = null; // completion promise of this frame's last submit
	const frames = []; // completion promises of the last frames, oldest first
	const submit = GPUQueue.prototype.submit;
	GPUQueue.prototype.submit = function (...a) {
		const r = submit.apply(this, a);
		const p = this.onSubmittedWorkDone();
		p.then(() => { p.settled = true; }, () => { p.settled = true; });
		cur = p;
		return r;
	};
	const raf = window.requestAnimationFrame.bind(window);
	window.requestAnimationFrame = (cb) => raf(function tick(ts) {
		if (cur) { frames.push(cur); cur = null; }
		while (frames.length > 2) frames.shift();
		// About to start frame k+1 with frames [k-1, k] submitted: wait for k-1 (2 frames in flight at most), then run
		// the frame at once rather than on a further rAF, which would add up to a display interval to every frame.
		const gate = frames.length === 2 ? frames[0] : null;
		if (gate && !gate.settled) { const go = () => cb(performance.now()); gate.then(go, go); return; }
		cb(ts);
	});
}

async function runRamp(ramp) {
	const browser = await launch();
	const page = await browser.newPage({ viewport: { width: W, height: H }, deviceScaleFactor: 1 });
	let result = false;
	let failure = '';
	let lastProgress = Date.now();
	let shotTaken = false;
	let finish;
	const finished = new Promise((r) => { finish = r; });
	const fail = (why) => { if (!failure && !result) { failure = why; finish(); } };
	page.on('console', (m) => {
		const t = m.text();
		console.log(t);
		if (t.startsWith('[RAMP] ')) lastProgress = Date.now();
		if (t.startsWith(`[BENCHMARK] ramp=${ramp} `)) result = true;
		if (t.includes('RAMP_ALL_DONE')) finish();
		if (/device (was )?lost/i.test(t)) fail('device_lost');
		if (/Parameter "mem" is null|out of memory|Cannot enlarge memory/i.test(t)) fail('out_of_memory');
		if (SHOTS && !shotTaken && /^\[RAMP\] ramp=\w+ step=2 phase=grow n=/.test(t)) {
			shotTaken = true;
			mkdirSync(SHOTS, { recursive: true });
			page.screenshot({ path: join(SHOTS, `${ramp}.png`), timeout: 15000 }).catch((e) => {
				console.log('[ramp_browser] screenshot failed: ' + e.message.split('\n')[0]);
				fail('screenshot_timeout');
			});
		}
	});
	page.on('pageerror', (e) => { console.log('[PAGE-ERROR] ' + e.message); fail('page_error'); });
	page.on('crash', () => { console.log('[PAGE-CRASH]'); fail('page_crash'); });
	await page.addInitScript(pageInit, PACE);
	const q = `ramps=${ramp}&target_fps=${TARGET}&max_s=${MAX_S}${MEM_MB ? `&mem_mb=${MEM_MB}` : ''}`;
	const t0 = Date.now();
	await page.goto(`http://127.0.0.1:${server.address().port}/?${q}`).catch((e) => fail('load_failed ' + e.message.split('\n')[0]));

	const envTimer = setTimeout(async () => {
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
			console.log(`[BENCHMARK] browser_env browser=${BROWSER} uncapped=${UNCAPPED ? 1 : 0} paced=${PACE && env.webgpu !== 'none' ? 1 : 0} canvas=${env.canvas} dpr=${env.dpr} software=${sw} webgpu_adapter=${env.webgpu} webgl_renderer=${env.webgl}`);
			if (sw) console.log('[BENCHMARK] WARNING: software adapter -- this run does not measure the GPU');
		} catch (e) {
			console.log('[BENCHMARK] browser_env error ' + e.message.split('\n')[0]);
		}
	}, 8000);
	const watchdog = setInterval(() => {
		if ((Date.now() - lastProgress) / 1000 > STALL_S) fail(`stalled_${STALL_S}s`);
		if ((Date.now() - t0) / 1000 > MAX_S + 120) fail('time_budget');
	}, 1000);

	await finished;
	clearInterval(watchdog);
	clearTimeout(envTimer);
	if (!result && !failure) failure = 'no_result';
	if (failure) console.log(`[BENCHMARK] ramp_failed ramp=${ramp} reason=${failure} after_s=${((Date.now() - t0) / 1000).toFixed(0)}`);
	console.log(`[ramp_browser] ${ramp}: ${failure ? 'FAILED ' + failure : 'done'} in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
	// close() can hang on a wedged GPU process; do not let it hang the run.
	await Promise.race([browser.close().catch(() => {}), new Promise((r) => setTimeout(r, 15000))]);
	return !failure;
}

let allOk = true;
for (const ramp of RAMPS) {
	if (!(await runRamp(ramp))) allOk = false;
}
console.log('[BENCHMARK] RAMP_ALL_DONE');
server.close();
// Exiting also takes down any browser whose close() timed out (Playwright kills its children on exit).
process.exit(allOk ? 0 : 1);
