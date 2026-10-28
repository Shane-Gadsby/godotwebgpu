// Preload-behind-the-start-screen test (task 53), from the camera-sim start_test.mjs and task 51's version.
// Parent on localhost:8701 (game iframe), game on 127.0.0.1:8702; every request the game server receives is logged.
// usage: node preload_test.mjs <chrome|firefox> <game_dir> <out_dir> <scenario> [--mbps N] [--direct]
//   ready:     wait for "Ready", stay idle 5 s (no rtenv, no wasm compile, no requestDevice), press, time to first frame.
//   early:     press at ~2 s after load (use with --mbps); bar must keep moving, game starts after the download.
//   savedata:  navigator.connection.saveData stubbed true: nothing but the page is fetched before the press.
//   nopreload: (preload off export) nothing fetched before the press; then press, run; reload; press again (warm cache).
//   noprogress:(preload_progress off) files still prefetched, no label shown.
//   fail:      the main pack answers 500 to its first 4 requests: prefetch fails, the press fetches it again.
import { createServer } from 'http';
import { readFile, mkdir, writeFile } from 'fs/promises';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium, firefox } = require('playwright');
const { FIREFOX_WEBGPU_PREFS } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu-53/webgpu_tests/scene_smoketest/firefox_prefs.mjs');

const args = process.argv.slice(2);
const [BROWSER, GAME, OUT, SCENARIO] = args;
const MBPS = args.includes('--mbps') ? Number(args[args.indexOf('--mbps') + 1]) : 0;
const DIRECT = args.includes('--direct');
// Cache-Control only for the warm-cache baseline (nopreload) or when asked: camera-sim's server sent none.
const CACHE = args.includes('--cache') || SCENARIO === 'nopreload';
const PARENT = '/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w53/parent';
await mkdir(OUT, { recursive: true });
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.pck': 'application/octet-stream' };
const requests = []; // { name, t, end }
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); if (!ok) failures++; };
let failPack = SCENARIO === 'fail' ? 4 : 0;
const tStartServer = Date.now();
function serve(dir, host, port) {
	return new Promise((resolve) => {
		const s = createServer(async (req, res) => {
			const name = decodeURIComponent(req.url.split('?')[0]);
			if (name === '/__blank') { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<!doctype html><title>blank</title>'); return; }
			const entry = { name, t: Date.now(), end: 0, status: 200, aborted: false, range: req.headers.range || '' };
			if (port === 8702) requests.push(entry);
			res.on('close', () => { if (!res.writableFinished) entry.aborted = true; });
			if (port === 8702 && name === '/index.pck' && failPack > 0) {
				failPack--; entry.status = 500; entry.end = Date.now();
				res.writeHead(500); res.end(); return;
			}
			const p = path.join(dir, name);
			let data;
			try { data = await readFile(p); } catch (e) { entry.status = 404; res.writeHead(404); res.end(); return; }
			// Cacheable, so the nopreload baseline's second visit is a warm-cache start.
			const headers = { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream', 'Content-Length': data.length };
			if (CACHE) headers['Cache-Control'] = 'max-age=3600';
			res.writeHead(200, headers);
			if (port !== 8702 || !MBPS || data.length < 256 * 1024) { res.end(data); entry.end = Date.now(); return; }
			// Throttled: 256 KiB pieces at MBPS MB/s.
			const piece = 256 * 1024;
			let off = 0;
			const tick = () => {
				if (off >= data.length) { res.end(); entry.end = Date.now(); return; }
				res.write(data.subarray(off, off + piece));
				off += piece;
				setTimeout(tick, (piece / (MBPS * 1e6)) * 1000);
			};
			tick();
		});
		s.listen(port, host, () => resolve(s));
	});
}
const s1 = await serve(PARENT, 'localhost', 8701);
const s2 = await serve(GAME, '127.0.0.1', 8702);

const browser = BROWSER === 'firefox'
	? await firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS })
	: await chromium.launch({
		headless: false,
		executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	});
const context = await browser.newContext({ viewport: { width: 1100, height: 800 } });
// Counters for what "running the engine" would do, in every frame: wasm compiles and GPU devices.
await context.addInitScript((saveData) => {
	window.__probe = { compiles: 0, devices: 0, firstFrame: 0 };
	const W = WebAssembly;
	for (const fn of ['compile', 'compileStreaming', 'instantiate', 'instantiateStreaming']) {
		const orig = W[fn];
		if (orig) {
			W[fn] = function (...a) { window.__probe.compiles++; return orig.apply(this, a); };
		}
	}
	if (window.GPUAdapter) {
		const orig = GPUAdapter.prototype.requestDevice;
		GPUAdapter.prototype.requestDevice = function (...a) { window.__probe.devices++; console.log(`PROBE requestDevice at ${performance.now().toFixed(0)} ms: ${new Error().stack}`); return orig.apply(this, a); };
	}
	for (const type of ['click', 'keydown', 'pointerdown']) {
		document.addEventListener(type, (e) => { console.log(`PROBE ${type} trusted=${e.isTrusted} target=${e.target && e.target.id} at ${performance.now().toFixed(0)} ms`); }, true);
	}
	window.addEventListener('godot-webgpu-first-frame', () => { window.__probe.firstFrame = performance.now(); });
	if (saveData) {
		Object.defineProperty(Navigator.prototype, 'connection', { configurable: true, get: () => ({ saveData: true }) });
	}
}, SCENARIO === 'savedata');
const page = await context.newPage();
const consoleMsgs = [];
page.on('console', (m) => {
	if (m.text().startsWith('PROBE')) console.log(`${((Date.now() - tStartServer) / 1000).toFixed(1)}s(srv)`, m.text().slice(0, 1500));
	if (m.type() === 'error' || m.type() === 'warning') consoleMsgs.push(`${m.type()}: ${m.text().slice(0, 300)}`);
});
page.on('pageerror', (e) => consoleMsgs.push(`pageerror: ${e.message}`));

// Adapter assertion, on the game origin (a secure context), before anything else.
await page.goto('http://127.0.0.1:8702/__blank');
const adapter = await page.evaluate(async () => {
	if (!navigator.gpu) return null;
	const a = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
	if (!a) return null;
	const info = a.info || {};
	return { vendor: info.vendor, architecture: info.architecture, fallback: !!(a.isFallbackAdapter || info.isFallbackAdapter), maxTex: a.limits.maxSampledTexturesPerShaderStage, bc: a.features.has('texture-compression-bc'), etc2: a.features.has('texture-compression-etc2') };
});
console.log('adapter', JSON.stringify(adapter));
if (BROWSER === 'firefox') {
	if (!adapter) { console.log('FAIL adapter: requestAdapter() returned null'); process.exit(3); }
} else if (!adapter || !/nvidia/i.test(adapter.vendor) || !/lovelace/i.test(adapter.architecture) || adapter.fallback || adapter.maxTex !== 48) {
	console.log('FAIL adapter is not the real GPU'); process.exit(3);
}
requests.length = 0;
consoleMsgs.length = 0;

let t0 = Date.now();
const T = () => `${((Date.now() - t0) / 1000).toFixed(1)}s`;
let frame;
async function open() {
	t0 = Date.now();
	if (DIRECT) {
		await page.goto('http://127.0.0.1:8702/index.html');
		frame = () => page.mainFrame();
	} else {
		await page.goto('http://localhost:8701/index.html');
		const find = () => page.frames().find((f) => f.url().startsWith('http://127.0.0.1:8702'));
		while (!find()) await page.waitForTimeout(50);
		frame = find;
	}
	if (SCENARIO !== 'auto') {
		await frame().waitForSelector('#status-start', { state: 'visible' });
	}
}
const state = () => frame().evaluate(() => ({
	rtenv: !!(typeof engine !== 'undefined' && engine && engine.rtenv),
	compiles: window.__probe.compiles,
	devices: window.__probe.devices,
	preload: (document.getElementById('status-preload') || {}).textContent,
	preloadShown: !!document.getElementById('status-preload') && getComputedStyle(document.getElementById('status-preload')).display !== 'none',
}));
const files = () => requests.map((r) => r.name).filter((n) => !/^\/(index\.html|index\.js|index\.png|favicon\.ico)$/.test(n));
const counts = () => requests.reduce((m, r) => { m[r.name] = (m[r.name] || 0) + 1; return m; }, {});

async function pressAndWait(label) {
	const tPress = Date.now();
	await frame().click('#status-start');
	console.log(T(), `pressed (${label})`);
	// Sample the loading bar while it is up.
	const bar = [];
	while (true) {
		const s = await frame().evaluate(() => {
			const st = document.getElementById('status');
			const p = document.getElementById('status-progress');
			return st ? { v: p.hasAttribute('value') ? p.value / (p.max || 1) : -1, label: (document.getElementById('status-label') || {}).textContent || '' } : null;
		});
		if (!s) break;
		bar.push({ t: Date.now() - tPress, ...s });
		if (Date.now() - tPress > 200000) break;
		await page.waitForTimeout(100);
	}
	const up = Date.now() - tPress;
	const ff = await frame().evaluate(() => window.__probe.firstFrame);
	console.log(T(), `${label}: overlay gone ${(up / 1000).toFixed(2)} s after the press`);
	return { tPress, up, bar, ff };
}

const result = { browser: BROWSER, scenario: SCENARIO, mbps: MBPS, adapter };
await open();
console.log(T(), 'start screen', JSON.stringify(await state()));

if (SCENARIO === 'auto') {
	// No start button (the option off): the game starts on its own, as before.
	await frame().waitForFunction(() => !document.getElementById('status'), null, { timeout: 200000, polling: 100 });
	result.loadToUp = Date.now() - t0;
	console.log(T(), 'auto: overlay gone');
} else if (SCENARIO === 'ready' || SCENARIO === 'fail' || SCENARIO === 'noprogress') {
	const deadline = Date.now() + 120000;
	// Ready = every prefetched file has its last byte served and the label (when shown) says so.
	while (Date.now() < deadline) {
		const s = await state();
		if (SCENARIO === 'noprogress' ? (files().length >= 3 && requests.every((r) => r.end)) : s.preload === 'Ready' || (SCENARIO === 'fail' && s.preload === '')) break;
		await page.waitForTimeout(100);
	}
	if (SCENARIO === 'noprogress') await page.waitForTimeout(1000);
	const sDone = await state();
	console.log(T(), 'download done', JSON.stringify(sDone), JSON.stringify(counts()));
	result.beforePress = counts();
	if (SCENARIO === 'noprogress') {
		check(!sDone.preloadShown, 'preload_progress off: no progress line under the button');
	} else if (SCENARIO === 'ready') {
		check(sDone.preload === 'Ready', `label says Ready (${sDone.preload})`);
	}
	check(files().some((n) => n.endsWith('.wasm')) && files().includes('/index.pck'), `wasm and main pack fetched before the press (${files().join(', ')})`);
	await page.waitForTimeout(5000);
	const sIdle = await state();
	console.log(T(), '5 s after the download', JSON.stringify(sIdle));
	check(!sIdle.rtenv && sIdle.compiles === 0 && sIdle.devices === 0, `engine idle 5 s after the download (rtenv ${sIdle.rtenv}, wasm compiles ${sIdle.compiles}, requestDevice ${sIdle.devices})`);
	const r = await pressAndWait(SCENARIO);
	result.pressToUp = r.up;
	result.bar = r.bar;
} else if (SCENARIO === 'early') {
	await page.waitForTimeout(Math.max(0, 2000 - (Date.now() - t0)));
	console.log(T(), 'before press', JSON.stringify(await state()));
	const r = await pressAndWait('early');
	const lastByte = Math.max(...requests.map((x) => x.end));
	const tFirstByteOfAll = Math.min(...requests.filter((x) => x.name !== '/index.html').map((x) => x.t));
	result.downloadSeconds = (lastByte - tFirstByteOfAll) / 1000;
	result.lastByteToUp = (r.tPress + r.up - lastByte) / 1000;
	console.log(`download took ${result.downloadSeconds.toFixed(1)} s; overlay gone ${result.lastByteToUp.toFixed(2)} s after the last byte`);
	// The bar while downloading: the part sampled before the last byte must keep rising.
	const during = r.bar.filter((b) => r.tPress + b.t < lastByte && b.v >= 0);
	let backwards = 0; let stalls = 0; let lastV = -1; let lastMoveT = 0;
	for (const b of during) {
		if (b.v < lastV - 1e-6) backwards++;
		if (b.v > lastV + 1e-6) { if (lastV >= 0 && b.t - lastMoveT > 1500) stalls++; lastMoveT = b.t; }
		lastV = b.v;
	}
	result.bar = r.bar;
	check(during.length > 10 && backwards === 0 && stalls === 0, `bar kept moving while downloading (${during.length} samples, from ${during.length ? during[0].v.toFixed(3) : '-'} to ${during.length ? during[during.length - 1].v.toFixed(3) : '-'}, ${backwards} backwards, ${stalls} stalls > 1.5 s)`);
	check(result.lastByteToUp > 0, 'game started after the download completed');
} else if (SCENARIO === 'savedata' || SCENARIO === 'nopreload') {
	await page.waitForTimeout(4000);
	const s = await state();
	console.log(T(), '4 s on the start screen', JSON.stringify(s), JSON.stringify(counts()));
	check(files().length === 0, `nothing but the page fetched before the press (${files().join(', ') || 'none'})`);
	check(!s.preloadShown, 'no download line');
	const r = await pressAndWait(SCENARIO);
	result.pressToUp = r.up;
	if (SCENARIO === 'nopreload') {
		// Warm cache: the same export again, every file from the HTTP cache.
		const before = requests.length;
		await open();
		await page.waitForTimeout(1000);
		const r2 = await pressAndWait('nopreload, cached');
		result.pressToUpCached = r2.up;
		result.cachedRequests = requests.slice(before).map((x) => x.name);
		console.log('cached run requests to the game server:', JSON.stringify(result.cachedRequests));
	}
}

const c = counts();
result.requests = c;
result.requestLog = requests.map((r) => ({ name: r.name, t: r.t - t0, end: r.end ? r.end - t0 : 0, status: r.status, aborted: r.aborted, range: r.range }));
for (const r of result.requestLog) if (r.aborted || r.range) console.log('request', JSON.stringify(r));
console.log('requests to the game server:', JSON.stringify(c));
const perFile = Object.entries(c).filter(([n]) => !/favicon/.test(n));
if (SCENARIO === 'fail') {
	check(c['/index.pck'] === 5, `main pack: 4 failed prefetch attempts, then fetched again on the press (${c['/index.pck']})`);
	check(perFile.filter(([n]) => n !== '/index.pck').every(([, k]) => k === 1), 'every other file requested once');
} else if (SCENARIO !== 'nopreload') {
	check(perFile.every(([, k]) => k === 1), `each file requested exactly once (${perFile.map(([n, k]) => `${n}:${k}`).join(' ')})`);
}
const tex = Object.keys(c).filter((n) => n.includes('.textures_'));
result.texturePack = tex;
console.log('texture packs fetched:', tex.join(', ') || 'none');
check(tex.length <= 1, 'at most one texture pack fetched');
await page.screenshot({ path: `${OUT}/${BROWSER}_${SCENARIO}_running.png` });
const errs = consoleMsgs.filter((m) => !/favicon|screen-wake-lock|Permissions policy/i.test(m));
result.console = errs;
if (errs.length) console.log('console:', errs.join('\n  '));
await writeFile(`${OUT}/${BROWSER}_${SCENARIO}.json`, JSON.stringify(result, null, 1));
await browser.close();
s1.close(); s2.close();
console.log(failures ? `RESULT FAIL (${failures})` : 'RESULT PASS');
process.exit(failures ? 1 : 0);
