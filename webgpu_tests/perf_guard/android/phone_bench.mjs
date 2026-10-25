// phone_bench.mjs: A/B frame time + GPU time, and Forward+ parity frames, of the same scenes exported with
// different converters, on the Razr 60 in Chrome *and Firefox* for Android (work task 12).
//
// The other tools in this folder drive Chrome over CDP. Firefox for Android has no CDP, so this one needs no
// debugging protocol at all: the measurement script is injected into the served index.html and reports back to
// this server over fetch(), and the browser is started with a plain VIEW intent. The same harness therefore runs
// in either browser, and on the desktop with --local <browser binary> (to validate the harness, not to measure).
//
//   node phone_bench.mjs --out <dir> --configs tint=<exports root>,sc=<exports root> \
//       [--scenes fp,heavy,postfx,platformer,stress] [--browsers chrome,firefox] [--runs 3] \
//       [--settle 20000] [--measure 10000] [--shots] [--local <browser binary>]
//   node phone_bench.mjs --sweep <feature ids|look|all> --out <dir> --configs ... --browsers ...   # Forward+ frames
//
// An <exports root> holds one export per scene directory (see SCENES). Each (run, scene, browser) measures every
// config, with the config order rotated per run (as results/08-gpu-timing/compare.sh does). Every launch writes
// <out>/<scene>.<browser>.<config>.<run>.json; summarize with phone_summary.mjs. A launch already on disk is not
// measured again, so an interrupted session resumes.
//
// Before each launch: battery under --max-temp (36 C), HAL GPU sensor under --max-gpu-temp (42 C), Mali devfreq
// cooling device at 0 (waits up to 10 min,
// then refuses -- it never measures a hot phone, whose throttled GPU would read as a regression); the screen must
// be on. Each launch force-stops the browser first (a cold process), and the page navigates itself to /__done
// when it has reported, so the tab Chrome or Firefox restores on the next start holds no engine (two engines on
// one GPU was how the first phone number went wrong). The user's own tabs are never closed.
//
// Sweep mode renders fp_check's configs at 640x360 with devicePixelRatio forced to 1 (the native reference's
// size), reads each frozen frame back with canvas.toBlob() and posts it here: <out>/frames/<browser>.<config>/
// <id>_<0|1>.png, comparable pixel for pixel with run_forward_plus.mjs's native reference.
import { createServer } from 'http';
import { readFileSync, writeFileSync, existsSync, mkdirSync, statSync, readdirSync } from 'fs';
import { execFileSync, spawn } from 'child_process';
import { join, extname, resolve } from 'path';

const argv = process.argv.slice(2);
const arg = (n, d) => { const i = argv.indexOf(`--${n}`); return i !== -1 ? argv[i + 1] : d; };
const OUT = resolve(arg('out', 'phone_bench_out'));
const CONFIGS = arg('configs', '').split(',').filter(Boolean).map((c) => { const j = c.indexOf('='); return [c.slice(0, j), resolve(c.slice(j + 1))]; });
const BROWSERS = arg('browsers', 'chrome').split(',');
const RUNS = +arg('runs', 3);
const SETTLE = +arg('settle', 20000), MEASURE = +arg('measure', 10000);
const PORT = +arg('port', 8099);
const MAX_TEMP = +arg('max-temp', 36);
const MAX_GPU_TEMP = +arg('max-gpu-temp', 42); // HAL GPU sensor, C: idle ~35, a launch ends near 60
const LOCAL = arg('local', ''); // desktop browser binary: validate the harness without a phone
const SHOTS = argv.includes('--shots');
const SWEEP = arg('sweep', '');
const BOOT_TIMEOUT = +arg('boot-timeout', 180000);
const SERIAL = arg('serial', process.env.ANDROID_SERIAL || '');

// Scene name -> export directory under an exports root, and the page script the smoketest injects for it.
const SCENES = {
	fp: { dir: 'fp' }, // Forward+ fixture, its boot configuration (fp_scene.gd animates the camera)
	heavy: { dir: 'heavy' }, // scene_i_forward_plus_heavy (SDFGI, volumetric fog, SSIL, SSAO, ...)
	postfx: { dir: 'benchmark_postfx' }, // Forward Mobile project (task 08's +2% on desktop)
	platformer: { dir: 'demo_3d_platformer', inject: 'window.stressConfig = { enabled: false };' },
	stress: { dir: 'demo_3d_platformer', inject: 'window.stressConfig = { mesh_count: 875, enemy_count: 38, shadow_light_count: 8, nonshadow_light_count: 11, particle_emitter_count: 6, particles_per_emitter: 1500, billboard_count: 150 };' },
};
const SCENE_LIST = arg('scenes', 'fp,heavy,postfx,platformer,stress').split(',');
// The Look strip's Forward+ configs: a spread over lighting, shadows, GI, post and the screen-space effects.
const LOOK_SET = ['ambient_light', 'directional_shadow', 'omni_shadow', 'spot_shadow', 'sdfgi', 'voxel_gi', 'ssao', 'ssil', 'ssr', 'glow', 'volumetric_fog', 'taa', 'fsr2', 'decal', 'reflection_probe', 'subsurface_scattering', 'dof_far'];

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json', '.svg': 'image/svg+xml', '.bin': 'application/octet-stream' };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const T0 = Date.now();
const log = (m) => console.log(`${new Date().toTimeString().slice(0, 8)} ${m}`);

const adb = (...x) => execFileSync('adb', SERIAL ? ['-s', SERIAL, ...x] : x, { encoding: 'utf8', maxBuffer: 64 << 20 });
const adbBuf = (...x) => execFileSync('adb', SERIAL ? ['-s', SERIAL, ...x] : x, { maxBuffer: 64 << 20 });
const PACKAGES = { chrome: ['com.android.chrome'], firefox: ['org.mozilla.firefox', 'org.mozilla.firefox_beta', 'org.mozilla.fenix'] };
let installed = null;
function pkgFor(browser) {
	if (!installed) installed = new Set(adb('shell', 'pm', 'list', 'packages').split('\n').map((l) => l.replace('package:', '').trim()));
	return PACKAGES[browser].find((p) => installed.has(p)) || null;
}
function phoneTemp() {
	try { const m = /temperature: (\d+)/.exec(adb('shell', 'dumpsys', 'battery')); return m ? +m[1] / 10 : null; } catch { return null; }
}
// The thermal HAL's *current* GPU temperature and the Mali devfreq cooling state. Not dumpsys's "Thermal Status"
// line: on the Razr it is a cached value that stayed at 1 (light) for 10 min with every current sensor cool.
function thermalStatus() {
	try {
		const d = adb('shell', 'dumpsys', 'thermalservice');
		const cur = d.slice(d.indexOf('Current temperatures from HAL'));
		const gpu = /mValue=([\d.]+), mType=1, mName=GPU/.exec(cur), mali = /CoolingDevice\{mValue=(\d+), mType=\d+, mName=devfreq-[^}]*mali/.exec(cur);
		return { gpu: gpu ? +gpu[1] : null, mali: mali ? +mali[1] : null };
	} catch { return { gpu: null, mali: null }; }
}
let displayId = null;
function screencap(file) {
	// A foldable has two displays; without -d screencap prints a warning instead of a PNG (memory "Android phone profiling").
	if (displayId === null) {
		const ids = [...adb('shell', 'dumpsys', 'SurfaceFlinger', '--display-id').matchAll(/Display (\d+)/g)].map((m) => m[1]);
		displayId = ids[0] || '';
	}
	const png = adbBuf('exec-out', 'screencap', ...(displayId ? ['-d', displayId] : []), '-p');
	if (png.slice(1, 4).toString() !== 'PNG') throw new Error('screencap returned no PNG: ' + png.slice(0, 80).toString());
	writeFileSync(file, png);
}

// --- the page side ----------------------------------------------------------------------------------------------
// Runs before the shell's own scripts. Reports: boot (first 30 submits), result (timing window), frame (sweep),
// error/log lines, then navigates to /__done.
function pageScript(cfg) {
	return `<script>(function(){
const CFG = ${JSON.stringify(cfg)};
// keepalive (so a report survives the page navigating away) caps the body at 64 KB; frames are larger.
const post = (o) => { const body = JSON.stringify(Object.assign({ launch: CFG.launch }, o)); return fetch('/__report', { method: 'POST', body, keepalive: body.length < 60000 }).catch((e) => console.warn('[phone_bench] report failed: ' + e)); };
const R = { submits: 0, errors: [], logs: [], adapter: null, banner: null, t0: performance.now() };
window.__pb = R;
if (CFG.dpr1) Object.defineProperty(window, 'devicePixelRatio', { get: () => 1 });
const BENIGN = /float32-blendable|AudioContext|_check_capabilities|Source map/i;
const keep = (kind, a) => { const t = Array.from(a).map((x) => (x && x.stack) ? x.stack : String(x)).join(' ');
	if (kind === 'log') { if (/WebGPU|Forward|Mobile|baked|shader|\\[FP\\]/.test(t) && R.logs.length < 300) R.logs.push(t); if (!R.banner && /WebGPU \\d/.test(t)) R.banner = t; if (/^\\[FP\\] (READY|UNKNOWN)/.test(t) && R.onFp) R.onFp(t); return; }
	if (BENIGN.test(t)) return; R.errors.push(kind + ': ' + t.slice(0, 600)); if (R.errors.length <= 20) post({ type: 'error', text: kind + ': ' + t.slice(0, 600) }); };
for (const k of ['log', 'warn', 'error']) { const f = console[k].bind(console); console[k] = (...a) => { keep(k === 'log' ? 'log' : k, a); f(...a); }; }
window.addEventListener('error', (e) => keep('uncaught', [e.message + ' ' + (e.filename || '') + ':' + (e.lineno || '')]));
window.addEventListener('unhandledrejection', (e) => keep('rejection', [e.reason]));
if (!navigator.gpu) { post({ type: 'fatal', text: 'navigator.gpu is undefined (no WebGPU in this browser)', ua: navigator.userAgent }); return; }
// --dump-wgsl <regex>: post every shader module whose label matches (one per distinct code); --swap-wgsl <dir>: replace
// each module whose label has a saved file (<label>.wgsl, from another export's dump) with that file's code -- the
// on-device bisection of a converter difference.
{ const cs = GPUDevice.prototype.createShaderModule; const seen = new Set(); const re = CFG.dump ? new RegExp(CFG.dump) : null;
	GPUDevice.prototype.createShaderModule = function (d) {
		const l = (d && d.label) || '';
		// One label covers every variant of a shader; the variants differ in their bindings (identical between the two
		// converters, which smoke.sh checks), so the binding list tells them apart.
		const b = [...new Set([...d.code.matchAll(/@(?:group\\((\\d+)u?\\)\\s*@binding\\((\\d+)u?\\)|binding\\((\\d+)u?\\)\\s*@group\\((\\d+)u?\\))/g)].map((m) => (m[1] ?? m[4]) + '.' + (m[2] ?? m[3])))].sort().join('-');
		let hb = 2166136261; for (let i = 0; i < b.length; i++) hb = Math.imul(hb ^ b.charCodeAt(i), 16777619) >>> 0; // short: names get long
		// Material versions of one shader share label and bindings: the n-th module created with that key is matched
		// with the n-th of the other export (creation order is the engine's, the same for both).
		const k0 = (l + '#' + hb.toString(16)).replace(/[^\\w:.#-]/g, '_'); const nth = (R.nth = R.nth || {})[k0] = (R.nth[k0] || 0) + 1;
		const key = k0 + '#' + nth;
		if (CFG.swap && CFG.swap[key] !== undefined) { R.swapped = (R.swapped || 0) + 1; d = Object.assign({}, d, { code: CFG.swap[key] }); }
		if (re && re.test(l) && !seen.has(key)) { seen.add(key); post({ type: 'wgsl', label: key, code: d.code }); }
		return cs.call(this, d);
	}; }
// GPU time per command buffer from timestamp queries: the same method as perf_guard/gpu_time.mjs without --passes.
const PAIRS = 2048;
const G = { on: false, used: 0, qs: null, device: null, enc: new WeakMap(), frames: 0 };
const rd = GPUAdapter.prototype.requestDevice;
GPUAdapter.prototype.requestDevice = async function (desc = {}) {
	const d = Object.assign({}, desc); const ai = this.info || {};
	R.adapter = { vendor: ai.vendor, architecture: ai.architecture, description: ai.description, device: ai.device, maxSampledTexturesPerShaderStage: this.limits.maxSampledTexturesPerShaderStage, timestamp: this.features.has('timestamp-query') };
	if (CFG.gpuTime && this.features.has('timestamp-query')) d.requiredFeatures = [...(desc.requiredFeatures || []), 'timestamp-query'];
	const dev = await rd.call(this, d);
	dev.addEventListener('uncapturederror', (e) => keep('gpu', [e.error && e.error.message]));
	dev.lost.then((i) => keep('device-lost', [i.reason + ' ' + i.message]));
	if (dev.features.has('timestamp-query')) { G.device = dev; G.qs = dev.createQuerySet({ type: 'timestamp', count: PAIRS * 2 }); }
	return dev;
};
const ce = GPUDevice.prototype.createCommandEncoder;
GPUDevice.prototype.createCommandEncoder = function (...a) {
	const e = ce.apply(this, a);
	if (G.on && this === G.device && G.used < PAIRS) { const k = G.used++; G.enc.set(e, k); e.beginComputePass({ timestampWrites: { querySet: G.qs, beginningOfPassWriteIndex: 2 * k } }).end(); }
	return e;
};
const fin = GPUCommandEncoder.prototype.finish;
GPUCommandEncoder.prototype.finish = function (...a) {
	const k = G.enc.get(this);
	if (k !== undefined) this.beginComputePass({ timestampWrites: { querySet: G.qs, endOfPassWriteIndex: 2 * k + 1 } }).end();
	return fin.apply(this, a);
};
const sub = GPUQueue.prototype.submit;
GPUQueue.prototype.submit = function (b) { R.submits++; if (R.submits === 30) onBoot(); return sub.call(this, b); };
// Frame intervals of the engine's own requestAnimationFrame loop.
const F = { on: false, times: [], last: 0 };
const raf = window.requestAnimationFrame.bind(window);
window.requestAnimationFrame = (cb) => raf((t) => { if (F.on) { if (F.last) F.times.push(t - F.last); F.last = t; G.frames++; } cb(t); });
const shaderStats = () => { const s = window.godotWebGPUShaderStats; return s ? { baked: s.baked, precompiled: s.precompiled, translated: s.translated, cached: s.cached, specialized: s.specialized } : null; };
const canvasInfo = () => { const c = document.getElementById('canvas'); return c ? [c.width, c.height] : null; };
async function gpuSpans() {
	if (!G.device || !G.used) return null;
	await new Promise((r) => setTimeout(r, 300));
	const n = G.used * 2;
	const res = G.device.createBuffer({ size: n * 8, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
	const rb = G.device.createBuffer({ size: n * 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
	const e = G.device.createCommandEncoder(); e.resolveQuerySet(G.qs, 0, n, res, 0); e.copyBufferToBuffer(res, 0, rb, 0, n * 8);
	G.device.queue.submit([e.finish()]); await rb.mapAsync(GPUMapMode.READ);
	const ts = new BigInt64Array(rb.getMappedRange().slice(0)); rb.destroy(); res.destroy();
	let busy = 0, cbs = 0; for (let k = 0; k < G.used; k++) { const a = Number(ts[2 * k]), b = Number(ts[2 * k + 1]); if (a > 0 && b >= a) { busy += (b - a) / 1e6; cbs++; } }
	return { busy, cbs };
}
const done = () => setTimeout(() => location.replace('/__done'), 500);
async function onBoot() {
	const bootMs = performance.now() - R.t0;
	post({ type: 'boot', bootMs, adapter: R.adapter, ua: navigator.userAgent });
	if (CFG.sweep) return sweep();
	await new Promise((r) => setTimeout(r, CFG.settle));
	Object.assign(F, { on: true, times: [], last: 0 }); Object.assign(G, { used: 0, frames: 0, enc: new WeakMap() }); G.on = !!G.qs;
	const t0 = performance.now();
	await new Promise((r) => setTimeout(r, CFG.measure));
	F.on = false; G.on = false; const wall = performance.now() - t0;
	const frames = F.times.length; let gs = null; try { gs = await gpuSpans(); } catch (e) { keep('gpu-time', [e]); }
	const t = F.times.slice().sort((a, b) => a - b); const q = (p) => t.length ? t[Math.min(t.length - 1, Math.floor(t.length * p))] : 0;
	const mean = frames ? F.times.reduce((s, x) => s + x, 0) / frames : 0;
	const sd = frames ? Math.sqrt(F.times.reduce((s, x) => s + (x - mean) * (x - mean), 0) / frames) : 0;
	post({ type: 'result', bootMs, wall, frames, fps: 1000 * frames / wall, meanMs: mean, sdMs: sd, p50: q(.5), p90: q(.9), p99: q(.99),
		gpuMsPerFrame: gs && G.frames ? gs.busy / G.frames : null, gpuCbs: gs ? gs.cbs : 0, gpuFrames: G.frames, gpuFull: G.used >= PAIRS,
		canvas: canvasInfo(), dpr: window.devicePixelRatio, inner: [innerWidth, innerHeight], adapter: R.adapter, banner: R.banner,
		shaders: shaderStats(), frameStats: window.godotWebGPUFrameStats || null, errors: R.errors, logs: R.logs.slice(0, 60), ua: navigator.userAgent });
	if (!CFG.hold) done();
}
// Sweep: drive fp_scene.gd's hash protocol, read each frozen frame back, post it as PNG.
async function sweep() {
	const ready = () => new Promise((res) => { const to = setTimeout(() => { R.onFp = null; res('TIMEOUT'); }, CFG.readyTimeout); R.onFp = (t) => { clearTimeout(to); R.onFp = null; res(t); }; });
	for (const id of CFG.sweep) for (const on of [0, 1]) {
		const p = ready(); location.hash = 'fp=' + id + ':' + on + ':' + CFG.fpSettle; const r = await p;
		if (!/READY/.test(r)) { post({ type: 'frame', id, on, status: r.startsWith('[FP] UNKNOWN') ? 'unknown' : 'timeout', errors: R.errors.splice(0) }); continue; }
		await new Promise((res) => raf(() => raf(res)));
		const c = document.getElementById('canvas');
		const blob = await new Promise((res) => c.toBlob(res, 'image/png'));
		const b64 = blob ? btoa(Array.from(new Uint8Array(await blob.arrayBuffer()), (x) => String.fromCharCode(x)).join('')) : null;
		await post({ type: 'frame', id, on, status: b64 ? 'ok' : 'no-blob', png: b64, canvas: canvasInfo(), errors: R.errors.splice(0) });
	}
	post({ type: 'sweep-done', swapped: R.swapped || 0, shaders: shaderStats(), adapter: R.adapter, banner: R.banner, ua: navigator.userAgent, logs: R.logs.slice(0, 60) });
	done();
}
})();</script>`;
}

// --- the host side ------------------------------------------------------------------------------------------------
let current = null; // { dir, cfg, inject, events: [], waiters }
const server = createServer((q, r) => {
	const url = q.url.split('?')[0].split('#')[0];
	const hdr = { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp', 'Cache-Control': 'no-store' };
	if (url === '/__report' && q.method === 'POST') {
		let body = ''; q.on('data', (d) => (body += d)); q.on('end', () => {
			r.writeHead(204, hdr); r.end();
			let m; try { m = JSON.parse(body); } catch { return; }
			if (!current || m.launch !== current.cfg.launch) return; // a late report from an earlier launch
			current.events.push(m);
			if (m.type === 'wgsl') { const d = join(OUT, 'wgsl', current.tag || 'x'); mkdirSync(d, { recursive: true }); writeFileSync(join(d, m.label + '.wgsl'), m.code); return; }
			if (m.type === 'error' || m.type === 'fatal') log(`    page ${m.type}: ${String(m.text).slice(0, 200)}`);
			for (const w of current.waiters.splice(0)) w(m);
		});
		return;
	}
	if (url === '/__done' || url === '/__blank') { r.writeHead(200, { ...hdr, 'Content-Type': 'text/html' }); r.end('<!doctype html><title>done</title><body style="background:#000"></body>'); return; }
	if (!current) { r.writeHead(404); r.end(); return; }
	const f = join(current.dir, url === '/' ? 'index.html' : decodeURIComponent(url));
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404, hdr); r.end(); return; }
	let body = readFileSync(f);
	if (f.endsWith('index.html')) {
		let html = body.toString('utf8');
		html = html.replace(/<head>/i, '<head>' + pageScript(current.cfg) + (current.inject ? `<script>${current.inject}</script>` : ''));
		if (current.cfg.fixedFps) html = html.replace(/"args":\[(\s*\])?/, (m, empty) => (empty ? '"args":["--fixed-fps","60"]' : '"args":["--fixed-fps","60",'));
		body = Buffer.from(html);
	}
	r.writeHead(200, { ...hdr, 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Content-Length': body.length });
	r.end(body);
});
await new Promise((res) => server.listen(PORT, '127.0.0.1', res));
if (!LOCAL) { adb('reverse', `tcp:${PORT}`, `tcp:${PORT}`); }

const next = (pred, ms) => new Promise((res) => {
	const hit = current.events.find(pred); if (hit) return res(hit);
	const to = setTimeout(() => res(null), ms);
	const w = (m) => { if (pred(m)) { clearTimeout(to); res(m); } else current.waiters.push(w); };
	current.waiters.push(w);
});

async function waitCool() {
	if (LOCAL) return {};
	for (let waited = 0; ; waited += 20) {
		const temp = phoneTemp(), th = thermalStatus();
		const gpuOk = th.gpu === null || th.gpu < MAX_GPU_TEMP, maliOk = !th.mali;
		if (temp !== null && temp < MAX_TEMP && gpuOk && maliOk) return { temp, gpuTemp: th.gpu, maliCooling: th.mali };
		if (waited >= 600) throw new Error(`phone stayed hot: battery ${temp} C, GPU ${th.gpu} C, Mali cooling ${th.mali}`);
		log(`  waiting for the phone to cool: battery ${temp} C, GPU ${th.gpu} C, Mali cooling state ${th.mali}`);
		await sleep(20000);
	}
}

let localProc = null;
async function launch(browser, url) {
	if (LOCAL) {
		const prof = join(OUT, `.profile-${browser}`); mkdirSync(prof, { recursive: true });
		if (/firefox/.test(LOCAL)) writeFileSync(join(prof, 'user.js'), ['dom.webgpu.enabled', 'gfx.webgpu.ignore-blocklist', 'gfx.webrender.all'].map((k) => `user_pref("${k}", true);`).join('\n') + '\nuser_pref("browser.shell.checkDefaultBrowser", false);\n');
		// PB_LOCAL_ARGS replaces the flags, e.g. a software adapter so a harness check does not touch the real GPU.
		const extra = process.env.PB_LOCAL_ARGS ? process.env.PB_LOCAL_ARGS.split(' ') : null;
		const a = /firefox/.test(LOCAL) ? ['-no-remote', '-profile', prof, ...(extra || []), url]
			: ['--user-data-dir=' + prof, '--no-first-run', ...(extra || ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist']), url];
		localProc = spawn(LOCAL, a, { stdio: 'ignore' });
		return;
	}
	const pkg = pkgFor(browser);
	if (!pkg) throw new Error(`${browser} is not installed on the phone`);
	adb('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP');
	adb('shell', 'wm', 'dismiss-keyguard'); // a swipe-only lock screen would cover the browser
	adb('shell', 'am', 'force-stop', pkg); await sleep(1500);
	// Chrome reuses the tab associated with this application id instead of opening a new one per launch.
	adb('shell', 'am', 'start', '-a', 'android.intent.action.VIEW', '-d', url, '-e', 'com.android.browser.application_id', 'phone_bench', pkg);
}
async function stop(browser) {
	if (LOCAL) { if (localProc) { localProc.kill(); localProc = null; } await sleep(1000); return; }
	await sleep(1500); // the page has navigated itself to /__done
	if (browser === 'chrome') {
		// Chrome opens a tab per launch and restores them all; close ours (only pages on this server) over the devtools
		// socket so the user's tab list does not grow by one per launch. Firefox has no such socket: there they stay
		// as blank /__done pages.
		try {
			adb('forward', 'tcp:9222', 'localabstract:chrome_devtools_remote');
			const list = await (await fetch('http://127.0.0.1:9222/json/list')).json();
			for (const t of list) if (t.type === 'page' && t.url.startsWith(`http://localhost:${PORT}/`)) await fetch(`http://127.0.0.1:9222/json/close/${t.id}`);
		} catch (e) { log('  could not close the test tab: ' + e.message); }
	}
	adb('shell', 'am', 'force-stop', pkgFor(browser));
}

let launchNo = 0;
async function measure(scene, browser, tag, root, run, retry = true) {
	const file = join(OUT, `${scene}.${browser}.${tag}.${run}.json`);
	if (existsSync(file)) { const o = JSON.parse(readFileSync(file, 'utf8')); if (o.result || o.fatal) return o; }
	const env = await waitCool();
	const sc = SCENES[scene];
	const cfg = { launch: `${Date.now()}-${++launchNo}`, settle: SETTLE, measure: MEASURE, gpuTime: true };
	current = { dir: join(root, sc.dir), cfg, inject: sc.inject || '', events: [], waiters: [] };
	if (!existsSync(join(current.dir, 'index.html'))) throw new Error(`no export at ${current.dir}`);
	log(`run ${run} ${scene} ${browser} ${tag} (battery ${env.temp ?? '-'} C, GPU ${env.gpuTemp ?? '-'} C)`);
	await launch(browser, `http://localhost:${PORT}/index.html?l=${cfg.launch}`);
	const boot = await next((m) => m.type === 'boot' || m.type === 'fatal', BOOT_TIMEOUT);
	if (!boot && !current.events.length && retry) {
		// Not one report: the page never ran (seen once on Chrome 155 right after a force-stop), not an engine failure.
		// Kept in the log, retried once; a second silent launch is recorded as a failure.
		log(`  no report at all from the page in ${BOOT_TIMEOUT / 1000} s; relaunching once`);
		await stop(browser);
		return measure(scene, browser, tag, root, run, false);
	}
	let res = null;
	if (boot && boot.type === 'boot') {
		log(`  booted in ${(boot.bootMs / 1000).toFixed(1)} s on ${boot.adapter ? boot.adapter.vendor + '/' + boot.adapter.architecture : '?'}`);
		res = await next((m) => m.type === 'result', SETTLE + MEASURE + 60000);
	}
	const shot = SHOTS && res && !LOCAL ? join(OUT, 'shots', `${scene}.${browser}.${tag}.${run}.png`) : null;
	if (shot) { mkdirSync(join(OUT, 'shots'), { recursive: true }); try { screencap(shot); } catch (e) { log('  screencap failed: ' + e.message); } }
	await stop(browser);
	const o = { scene, browser, tag, run, env, endTemp: LOCAL ? null : phoneTemp(), boot, result: res, fatal: current.events.find((m) => m.type === 'fatal') || null,
		errors: current.events.filter((m) => m.type === 'error').map((m) => m.text), shot };
	if (!boot) o.fatal = o.fatal || { text: `no boot report within ${BOOT_TIMEOUT / 1000} s` };
	else if (!res) o.fatal = { text: 'booted but no result' };
	writeFileSync(file, JSON.stringify(o, null, 1));
	if (res) log(`  fps ${res.fps.toFixed(1)}  frame ${res.meanMs.toFixed(2)} ms (p50 ${res.p50.toFixed(1)} p90 ${res.p90.toFixed(1)})  GPU ${res.gpuMsPerFrame !== null ? res.gpuMsPerFrame.toFixed(2) + ' ms/frame' : 'n/a'}  errors ${res.errors.length}  shaders ${JSON.stringify(res.shaders)}`);
	else log(`  FAILED: ${o.fatal.text}`);
	return o;
}

async function sweepRun(browser, tag, root, ids) {
	const dir = join(OUT, 'frames', `${browser}.${tag}`); mkdirSync(dir, { recursive: true });
	const env = await waitCool();
	const cfg = { launch: `${Date.now()}-${++launchNo}`, sweep: ids, fpSettle: +arg('fp-settle', 240), readyTimeout: 180000, dpr1: true, fixedFps: true, gpuTime: false };
	if (arg('dump-wgsl')) cfg.dump = arg('dump-wgsl');
	const swapDir = argv.flatMap((x, i) => (x === '--swap-wgsl' ? [argv[i + 1]] : []))[0];
	if (swapDir && tag !== 'tint') { cfg.swap = {}; for (const f of readdirSync(swapDir).filter((x) => x.endsWith('.wgsl'))) cfg.swap[f.slice(0, -5)] = readFileSync(join(swapDir, f), 'utf8'); log(`  swapping ${Object.keys(cfg.swap).length} modules from ${swapDir}`); }
	current = { dir: join(root, 'fp'), cfg, inject: '', events: [], waiters: [], tag };
	log(`sweep ${browser} ${tag}: ${ids.length} configs (phone ${env.temp ?? '-'} C)`);
	await launch(browser, `http://localhost:${PORT}/index.html?l=${cfg.launch}`);
	const boot = await next((m) => m.type === 'boot' || m.type === 'fatal', BOOT_TIMEOUT);
	if (!boot || boot.type !== 'boot') { log(`  FAILED to boot: ${boot ? boot.text : 'timeout'}`); await stop(browser); return; }
	let got = 0;
	for (;;) {
		const m = await next((x) => (x.type === 'frame' || x.type === 'sweep-done') && !x.seen, 240000);
		if (!m) { log('  sweep stalled'); break; }
		m.seen = true;
		if (m.type === 'sweep-done') { writeFileSync(join(dir, 'sweep.json'), JSON.stringify({ env, boot, done: m }, null, 1)); break; }
		if (m.status === 'ok') writeFileSync(join(dir, `${m.id}_${m.on}.png`), Buffer.from(m.png, 'base64'));
		got++; log(`  ${m.id}_${m.on}: ${m.status}${m.errors.length ? ' errors: ' + m.errors[0].slice(0, 120) : ''}`);
	}
	await stop(browser);
}

mkdirSync(OUT, { recursive: true });
try {
	if (SWEEP) {
		let ids = SWEEP === 'look' ? LOOK_SET : SWEEP.split(',');
		if (SWEEP === 'all') ids = (await import('../../forward_plus/features.mjs')).FEATURES.map((f) => f.id);
		for (const browser of BROWSERS) for (const [tag, root] of CONFIGS) await sweepRun(browser, tag, root, ids);
	} else {
		for (let run = 1; run <= RUNS; run++) for (const scene of SCENE_LIST) for (const browser of BROWSERS) {
			for (let i = 0; i < CONFIGS.length; i++) {
				const [tag, root] = CONFIGS[(i + run - 1) % CONFIGS.length];
				await measure(scene, browser, tag, root, run);
			}
		}
	}
} finally {
	server.close();
	if (localProc) localProc.kill();
}
log(`done in ${((Date.now() - T0) / 60000).toFixed(1)} min`);
process.exit(0);
