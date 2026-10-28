// Start-screen download text test (task 61), built on task 53's preload_test.mjs.
// The game is served on 127.0.0.1:8702 (direct, or in a cross-origin iframe from localhost:8701 with --iframe), and the
// server gates every downloaded file so each state of the line under the Start button holds still long enough to check:
//   hold: no file answers yet               -> the "downloading" message (total size unknown)
//   half: headers + half of every file sent -> the "progress" message at a fixed percentage
//   go:   the rest is sent                  -> the "ready" message
// then the Start button is pressed and the game must come up.
// usage: node preload_text_test.mjs <chrome|firefox> <game_dir> <out_dir> <name> <expect.json> [--iframe]
// expect.json: { downloading: string, progress: string (with {percent}) , ready: string, color: "rgb(r, g, b)" }, where
// an empty string is a message switched off. Writes <out>/<browser>_<name>.json and the three screenshots.
import { createServer } from 'http';
import { readFile, mkdir, writeFile } from 'fs/promises';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium, firefox } = require('playwright');
const { FIREFOX_WEBGPU_PREFS } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu-61/webgpu_tests/scene_smoketest/firefox_prefs.mjs');

const args = process.argv.slice(2);
const [BROWSER, GAME, OUT, NAME, EXPECT_FILE] = args;
const IFRAME = args.includes('--iframe');
// The config's fileSizes give the preloader its total at once, so "Downloading..." is up for one frame only; an
// export with fileSizes stripped never has a total, so it holds that message until "Ready" (--strict-hold).
const STRICT_HOLD = args.includes('--strict-hold');
const EXPECT = JSON.parse(await readFile(EXPECT_FILE, 'utf8'));
const PARENT = '/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w61/parent';
await mkdir(OUT, { recursive: true });
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.pck': 'application/octet-stream' };
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); if (!ok) failures++; };

// The gate. Page files (html, js, splash) are never held.
let phase = 'hold';
let phaseWaiters = [];
const setPhase = (p) => { phase = p; const w = phaseWaiters; phaseWaiters = []; w.forEach((f) => f()); };
const waitPhase = (ok) => new Promise((resolve) => { const t = () => (ok(phase) ? resolve() : phaseWaiters.push(t)); t(); });
const requests = [];
function serve(dir, host, port) {
	return new Promise((resolve) => {
		const s = createServer(async (req, res) => {
			const name = decodeURIComponent(req.url.split('?')[0]);
			if (name === '/__blank') { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<!doctype html><title>blank</title>'); return; }
			const entry = { name, end: 0 };
			if (port === 8702) requests.push(entry);
			let data;
			try { data = await readFile(path.join(dir, name)); } catch (e) { res.writeHead(404); res.end(); entry.end = 1; return; }
			const gated = port === 8702 && !/^\/(index\.html|index\.js|index\.png|favicon\.ico)$/.test(name);
			if (gated) await waitPhase((p) => p !== 'hold');
			res.writeHead(200, { 'Content-Type': MIME[path.extname(name)] || 'application/octet-stream', 'Content-Length': data.length });
			if (gated && phase === 'half') {
				const half = Math.floor(data.length / 2);
				res.write(data.subarray(0, half));
				await waitPhase((p) => p === 'go');
				res.end(data.subarray(half));
			} else {
				res.end(data);
			}
			entry.end = Date.now();
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
// Every textContent written to the line, in order, and whether anything else (innerHTML) touched it.
await context.addInitScript(() => {
	window.__preloadLog = [];
	const desc = Object.getOwnPropertyDescriptor(Node.prototype, 'textContent');
	Object.defineProperty(Node.prototype, 'textContent', {
		configurable: true, enumerable: desc.enumerable, get: desc.get,
		set(v) { if (this.id === 'status-preload') window.__preloadLog.push(String(v)); desc.set.call(this, v); },
	});
	const ih = Object.getOwnPropertyDescriptor(Element.prototype, 'innerHTML');
	Object.defineProperty(Element.prototype, 'innerHTML', {
		configurable: true, enumerable: ih.enumerable, get: ih.get,
		set(v) { if (this.id === 'status-preload') window.__preloadLog.push(`INNERHTML:${v}`); ih.set.call(this, v); },
	});
});
const page = await context.newPage();
const consoleMsgs = [];
page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') consoleMsgs.push(`${m.type()}: ${m.text().slice(0, 300)} ${(m.location() || {}).url || ''}`); });
page.on('response', (r) => { if (r.status() >= 400) consoleMsgs.push(`http ${r.status()} ${r.url()}`); });
page.on('pageerror', (e) => consoleMsgs.push(`pageerror: ${e.message}`));

// Adapter assertion, on the game origin (a secure context), before anything else.
await page.goto('http://127.0.0.1:8702/__blank');
const adapter = await page.evaluate(async () => {
	if (!navigator.gpu) return null;
	const a = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
	if (!a) return null;
	const info = a.info || {};
	return { vendor: info.vendor, architecture: info.architecture, fallback: !!(a.isFallbackAdapter || info.isFallbackAdapter), maxTex: a.limits.maxSampledTexturesPerShaderStage };
});
console.log('adapter', JSON.stringify(adapter));
if (BROWSER === 'firefox') {
	if (!adapter) { console.log('FAIL adapter: requestAdapter() returned null'); process.exit(3); }
} else if (!adapter || !/nvidia/i.test(adapter.vendor) || !/lovelace/i.test(adapter.architecture) || adapter.fallback || adapter.maxTex !== 48) {
	console.log('FAIL adapter is not the real GPU'); process.exit(3);
}
requests.length = 0;

const t0 = Date.now();
const T = () => `${((Date.now() - t0) / 1000).toFixed(1)}s`;
let frame;
if (IFRAME) {
	await page.goto('http://localhost:8701/index.html');
	const find = () => page.frames().find((f) => f.url().startsWith('http://127.0.0.1:8702'));
	while (!find()) await page.waitForTimeout(50);
	frame = find;
} else {
	await page.goto('http://127.0.0.1:8702/index.html');
	frame = () => page.mainFrame();
}
await frame().waitForSelector('#status-start', { state: 'visible' });
const state = () => frame().evaluate(() => {
	const el = document.getElementById('status-preload');
	const r = el.getBoundingClientRect();
	const b = document.getElementById('status-start').getBoundingClientRect();
	const cs = getComputedStyle(el);
	return {
		text: el.textContent, elements: el.childElementCount, display: cs.display, color: cs.color, opacity: cs.opacity,
		fontFamily: cs.fontFamily, fontSize: cs.fontSize,
		line: [r.x, r.y, r.width, r.height].map((v) => Math.round(v * 100) / 100),
		button: [b.x, b.y, b.width, b.height].map((v) => Math.round(v * 100) / 100),
		log: window.__preloadLog.slice(),
	};
});
async function shot(tag) {
	const file = `${OUT}/${BROWSER}_${NAME}_${tag}.png`;
	if (IFRAME) {
		await (await page.$('#game')).screenshot({ path: file });
	} else {
		await page.screenshot({ path: file });
	}
	return file;
}
// Waits until the line's log has not changed for `quiet` ms.
async function settle(quiet, limit) {
	const deadline = Date.now() + limit;
	let last = -1; let since = Date.now();
	while (Date.now() < deadline) {
		const n = (await state()).log.length;
		if (n !== last) { last = n; since = Date.now(); }
		if (Date.now() - since >= quiet) return true;
		await page.waitForTimeout(100);
	}
	return false;
}

const result = { browser: BROWSER, name: NAME, iframe: IFRAME, adapter, expect: EXPECT, states: {} };
const tpl = EXPECT.progress;
const escape = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const progressRe = new RegExp(`^${tpl.split('{percent}').map(escape).join('(\\d+)')}$`);
const isProgress = (t) => {
	if (!tpl) return false;
	if (!tpl.includes('{percent}')) return t === tpl;
	const m = progressRe.exec(t);
	return !!m && m.slice(1).every((p) => p === m[1]);
};
const percentOf = (t) => { const m = progressRe.exec(t); return m && m.length > 1 ? Number(m[1]) : null; };

// hold: the downloading message.
await page.waitForTimeout(1500);
const A = await state();
result.states.hold = A;
result.shots = { hold: await shot('hold') };
console.log(T(), 'hold', JSON.stringify(A));
check(A.log[0] === EXPECT.downloading, `first message is ${JSON.stringify(EXPECT.downloading)} (${JSON.stringify(A.log[0])})`);
if (STRICT_HOLD) {
	check(A.text === EXPECT.downloading, `size unknown: line still shows ${JSON.stringify(EXPECT.downloading)} (${JSON.stringify(A.text)})`);
} else {
	check(A.text === EXPECT.downloading || (isProgress(A.text) && (percentOf(A.text) || 0) === 0) || (!tpl && A.text === ''), `nothing arrived: line shows the first or the 0% message (${JSON.stringify(A.text)})`);
}

// half: a fixed percentage.
setPhase('half');
check(await settle(2500, 30000), 'line settled with half of every file sent');
const B = await state();
result.states.half = B;
result.shots.half = await shot('half');
console.log(T(), 'half', JSON.stringify(B));
if (STRICT_HOLD) {
	// Without fileSizes the preloader never has a total (preloader.js), so no percentage is ever shown.
	check(B.text === EXPECT.downloading, `no total without fileSizes: line still shows ${JSON.stringify(EXPECT.downloading)} (${JSON.stringify(B.text)})`);
} else if (tpl) {
	check(isProgress(B.text), `downloading: line matches ${JSON.stringify(tpl)} (${JSON.stringify(B.text)})`);
	if (tpl.includes('{percent}')) {
		const p = percentOf(B.text);
		check(p !== null && p >= 40 && p <= 60, `percentage about half (${p})`);
	}
} else {
	check(B.text === '', `progress message off: line empty while downloading (${JSON.stringify(B.text)})`);
}

// go: the ready message.
setPhase('go');
const deadline = Date.now() + 60000;
while (Date.now() < deadline && !(requests.length >= 3 && requests.every((r) => r.end))) await page.waitForTimeout(100);
check(await settle(2000, 20000), 'line settled after the last byte');
const C = await state();
result.states.go = C;
result.shots.go = await shot('go');
console.log(T(), 'go', JSON.stringify(C));
check(C.text === EXPECT.ready, `downloaded: line shows ${JSON.stringify(EXPECT.ready)} (${JSON.stringify(C.text)})`);

// The whole sequence: only the configured messages, percentages never backwards, text never HTML.
const log = C.log;
let lastP = -1; let backwards = 0; let unexpected = [];
for (const t of log) {
	if (t.startsWith('INNERHTML:')) { unexpected.push(t); continue; }
	if (t === EXPECT.downloading || t === EXPECT.ready || t === '') continue;
	if (isProgress(t)) { const p = percentOf(t); if (p !== null) { if (p < lastP) backwards++; lastP = p; } continue; }
	unexpected.push(t);
}
console.log('line log', JSON.stringify(log));
check(unexpected.length === 0, `only the configured messages were written, as text (${JSON.stringify(unexpected)})`);
check(backwards === 0, 'percentage never went backwards');
for (const [k, s] of Object.entries(result.states)) {
	check(s.elements === 0, `${k}: line holds text only, no elements`);
	check(s.display === 'block', `${k}: line shown`);
	check(s.color === EXPECT.color, `${k}: color ${EXPECT.color} (${s.color})`);
}
// The text is centered, so only the line's top and height must hold still.
const vbox = (s) => JSON.stringify([s.line[1], s.line[3]]);
check(vbox(A) === vbox(B) && vbox(B) === vbox(C), `line top and height unchanged across the states (${vbox(A)}, ${vbox(B)}, ${vbox(C)})`);
check(JSON.stringify(A.button) === JSON.stringify(B.button) && JSON.stringify(B.button) === JSON.stringify(C.button), `button did not move (${JSON.stringify(A.button)})`);

// The press still starts the game.
const tPress = Date.now();
await frame().click('#status-start');
let up = false;
while (Date.now() - tPress < 120000) {
	if (!(await frame().evaluate(() => !!document.getElementById('status')))) { up = true; break; }
	await page.waitForTimeout(100);
}
result.pressToUp = Date.now() - tPress;
check(up, `game up after the press (${(result.pressToUp / 1000).toFixed(2)} s)`);
const errs = consoleMsgs.filter((m) => !/favicon|screen-wake-lock|Permissions policy/i.test(m));
result.console = errs;
if (errs.length) console.log('console:', errs.join('\n  '));
await writeFile(`${OUT}/${BROWSER}_${NAME}.json`, JSON.stringify(result, null, 1));
await browser.close();
s1.close(); s2.close();
console.log(failures ? `RESULT FAIL (${failures})` : 'RESULT PASS');
process.exit(failures ? 1 : 0);
