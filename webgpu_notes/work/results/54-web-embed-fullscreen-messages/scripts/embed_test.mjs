// Task 54: fullscreen driven by the embedding page (html/embed_fullscreen/*), adapted from camera-sim's embed_test.mjs
// and start_test.mjs. Parent on localhost:8701, game on 127.0.0.1:8702 (two origins).
// usage: node embed_test.mjs <chrome|firefox> <exports_root> <out_dir> [--only s1,s2,...]
// <exports_root> holds the exports: main (embed on), start (embed + start button), deny (allowed_origins elsewhere),
// allow (allowed_origins = the parent), off (default options).
import { createServer } from 'http';
import { readFile, mkdir, writeFile } from 'fs/promises';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium, firefox } = require('playwright');
const { FIREFOX_WEBGPU_PREFS } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu-54/webgpu_tests/scene_smoketest/firefox_prefs.mjs');

const args = process.argv.slice(2);
const [BROWSER, ROOT, OUT] = args;
const ONLY = args.includes('--only') ? args[args.indexOf('--only') + 1].split(',') : null;
const PARENT = path.join(path.dirname(new URL(import.meta.url).pathname), 'parent');
await mkdir(OUT, { recursive: true });
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.pck': 'application/octet-stream', '.bin': 'application/octet-stream' };
function serve(dir, host, port) {
	return new Promise((resolve) => {
		const s = createServer(async (req, res) => {
			if (req.url === '/blank') { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<!doctype html><title>blank</title>'); return; }
			const p = path.join(dir, decodeURIComponent(req.url.split('?')[0]));
			try {
				const data = await readFile(p);
				res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream' });
				res.end(data);
			} catch (e) { res.writeHead(404); res.end(); }
		});
		s.listen(port, host, () => resolve(s));
	});
}
const s1 = await serve(PARENT, 'localhost', 8701);
const s2 = await serve(ROOT, '127.0.0.1', 8702);

let failures = 0;
const results = [];
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); results.push({ ok: !!ok, what }); if (!ok) failures++; };
const note = (what) => { console.log(`     ${what}`); results.push({ note: what }); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const eq = (a, b) => a[0] === b[0] && a[1] === b[1];
const WINDOWED = 0, FULLSCREEN = 3;

const browser = BROWSER === 'firefox'
	? await firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS })
	: await chromium.launch({
		executablePath: '/usr/bin/google-chrome-stable', headless: false,
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
		ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	});

// Every frame: record each 'message' listener added to window (with the stack), so the option-off case can show none.
const HOOK = () => {
	window.__msgListeners = [];
	const orig = EventTarget.prototype.addEventListener;
	EventTarget.prototype.addEventListener = function (type, fn, opts) {
		if (type === 'message' && this === window) {
			window.__msgListeners.push(String(new Error().stack).split('\n').slice(1, 4).join(' | '));
		}
		return orig.call(this, type, fn, opts);
	};
};

async function adapterCheck() {
	const page = await browser.newPage();
	await page.goto('http://127.0.0.1:8702/blank');
	const a = await page.evaluate(async () => {
		if (!navigator.gpu) return null;
		const ad = await navigator.gpu.requestAdapter();
		if (!ad) return null;
		const info = ad.info || {};
		return { vendor: info.vendor, architecture: info.architecture, fallback: !!info.isFallbackAdapter, maxSampled: ad.limits.maxSampledTexturesPerShaderStage };
	});
	await page.close();
	if (BROWSER === 'firefox') {
		check(!!a, `adapter (firefox) requestAdapter() non-null: ${JSON.stringify(a)}`);
		return !!a;
	}
	const ok = a && a.vendor === 'nvidia' && a.architecture === 'lovelace' && !a.fallback && a.maxSampled === 48;
	check(ok, `adapter (chrome) nvidia/lovelace/not fallback/48: ${JSON.stringify(a)}`);
	return ok;
}

async function openParent(game, query = '') {
	const context = await browser.newContext({ viewport: null });
	await context.addInitScript(HOOK);
	const page = await context.newPage();
	const consoleLines = [];
	page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') consoleLines.push(`${m.type()}: ${m.text().slice(0, 300)}`); });
	await page.goto(`http://localhost:8701/parent.html?game=${game}${query}`);
	let f;
	for (let i = 0; i < 100 && !(f = page.frames().find((fr) => fr.url().startsWith('http://127.0.0.1:8702/'))); i++) await sleep(100);
	const frame = () => page.frames().find((fr) => fr.url().startsWith('http://127.0.0.1:8702/'));
	const evil = () => page.frames().find((fr) => fr.url().endsWith('/evil.html'));
	const waitReady = async () => frame().waitForFunction(() => (window.__log || []).some((e) => e.tag === 'ready'), null, { timeout: 120000, polling: 200 });
	const snap = async () => {
		await frame().evaluate(() => { window.__cmd = 'report'; });
		await frame().waitForFunction(() => window.__cmd === '', null, { timeout: 10000 });
		const g = await frame().evaluate(() => {
			const c = document.getElementById('canvas');
			const log = window.__log;
			const m = /scale\(([\d.e+-]+)\)/.exec(c.style.transform || '');
			return {
				godot: log[log.length - 1], canvas: [c.width, c.height], css: [parseFloat(c.style.width), parseFloat(c.style.height)],
				scale: m ? Number(m[1]) : null, inner: [innerWidth, innerHeight], dpr: devicePixelRatio,
				ownFs: document.fullscreenElement ? (document.fullscreenElement.id || document.fullscreenElement.tagName) : null,
				log: log.slice(),
			};
		});
		g.parentFs = await page.evaluate(() => (document.fullscreenElement ? document.fullscreenElement.id : null));
		g.parentLog = await page.evaluate(() => window.__parentLog.slice());
		return g;
	};
	const waitFor = async (pred, timeout = 8000) => {
		const t0 = Date.now();
		let s;
		while (Date.now() - t0 < timeout) {
			s = await snap();
			if (pred(s)) return { s, ms: Date.now() - t0, ok: true };
			await sleep(100);
		}
		return { s, ms: Date.now() - t0, ok: false };
	};
	// A real click on the game canvas at canvas pixel (x, y).
	const clickCanvas = async (x, y) => {
		const el = await frame().$('#canvas');
		const box = await el.boundingBox();
		const s = await snap();
		await page.mouse.click(box.x + x * box.width / s.canvas[0], box.y + y * box.height / s.canvas[1]);
	};
	return { context, page, frame, evil, waitReady, snap, waitFor, clickCanvas, consoleLines };
}

// The engine's windowed state is consistent: Godot's window/viewport/corner == canvas, and the shell's scale fits it.
function consistent(s, label) {
	const g = s.godot;
	check(eq(g.window, s.canvas) && eq(g.visible_rect, s.canvas) && eq(g.ds, s.canvas) && eq(g.corner_label, s.canvas),
		`${label}: window ${g.window} visible ${g.visible_rect} corner ${g.corner_label} == canvas ${s.canvas}`);
	const fit = Math.min(s.inner[0] / s.css[0], s.inner[1] / s.css[1]);
	check(s.scale !== null && Math.abs(s.scale - fit) < 1e-3, `${label}: shell scale ${s.scale} fits (${fit.toFixed(4)}; css ${s.css}, iframe ${s.inner})`);
}
function fullSize(s) {
	return [Math.floor(s.inner[0] * s.dpr), Math.floor(s.inner[1] * s.dpr)];
}
const shot = (p, name) => p.page.screenshot({ path: path.join(OUT, `${BROWSER}_${name}.png`) });
const exitParent = (p) => p.page.evaluate(() => document.fullscreenElement && document.exitFullscreen());

const scenarios = {
	// 1, 2, 3, 5 (sibling frame, self-post), 8 (request path), on the default parent.
	async main() {
		const p = await openParent('main');
		await p.waitReady();
		await sleep(1000);
		const s0 = await p.snap();
		check(s0.godot.mode === WINDOWED, `main: starts windowed (mode ${s0.godot.mode})`);
		consistent(s0, 'main windowed');
		const listeners = await p.frame().evaluate(() => window.__msgListeners);
		note(`main: game frame window 'message' listeners: ${listeners.length}`);
		check(s0.parentLog.some((e) => e.what.startsWith('got godot:ready from http://127.0.0.1:8702')), 'main: parent got godot:ready');

		// 5: a sibling frame and the game window itself post the parent's message: ignored.
		await p.evil().evaluate(() => window.attack(true));
		await p.frame().evaluate(() => window.postMessage({ type: 'godot:fullscreen', on: true }, '*'));
		await sleep(1500);
		const s5 = await p.snap();
		check(s5.godot.mode === WINDOWED && eq(s5.canvas, s0.canvas) && s5.godot.signals === s0.godot.signals,
			`5 security: sibling-frame and self-posted {on:true} ignored (mode ${s5.godot.mode}, canvas ${s5.canvas}, signals ${s5.godot.signals})`);

		// 1: the parent's button.
		await p.page.click('#game-fullscreen');
		const w1 = await p.waitFor((s) => s.godot.mode === FULLSCREEN && eq(s.canvas, fullSize(s)) && eq(s.godot.window, s.canvas));
		const s1 = w1.s;
		check(w1.ok, `1 parent button: mode ${s1.godot.mode}, canvas ${s1.canvas} == iframe ${s1.inner} x dpr ${s1.dpr} (${w1.ms} ms)`);
		check(s1.parentFs === 'game' && s1.ownFs === null, `1: parent holds the iframe (parent fs ${s1.parentFs}, game fs ${s1.ownFs})`);
		check(s1.scale === 1, `1: shell CSS scale 1 (${s1.scale})`);
		check(s1.godot.signals > s0.godot.signals, `1: size_changed fired (${s0.godot.signals} -> ${s1.godot.signals})`);
		consistent(s1, '1 fullscreen');
		await shot(p, '1_parent_fullscreen');

		// 2: Esc, or the parent's exitFullscreen() if a synthetic Esc does not leave fullscreen.
		await p.page.keyboard.press('Escape');
		await sleep(1500);
		let how = 'Escape';
		if (await p.page.evaluate(() => !!document.fullscreenElement)) { how = 'parent exitFullscreen()'; await exitParent(p); }
		const w2 = await p.waitFor((s) => s.godot.mode === WINDOWED && eq(s.canvas, s0.canvas) && eq(s.godot.window, s0.canvas));
		check(w2.ok, `2 exit via ${how}: windowed, canvas ${w2.s.canvas} == ${s0.canvas} (${w2.ms} ms)`);
		await sleep(300);
		consistent(await p.snap(), '2 windowed again');
		await shot(p, '2_exited');

		// 3: the game calls window_set_mode(WINDOWED) while the parent holds fullscreen.
		await p.page.click('#game-fullscreen');
		check((await p.waitFor((s) => s.godot.mode === FULLSCREEN && eq(s.canvas, fullSize(s)))).ok, '3: parent fullscreen again');
		await p.frame().evaluate(() => { window.__cmd = 'windowed'; });
		await p.frame().waitForFunction(() => window.__cmd === '', null, { timeout: 10000 });
		const w3 = await p.waitFor((s) => s.parentFs === null && s.godot.mode === WINDOWED && eq(s.canvas, s0.canvas));
		const after = w3.s.log.filter((e) => e.tag === 'after_windowed').pop();
		check(w3.ok, `3 window_set_mode(WINDOWED): parent left fullscreen, game windowed, canvas ${w3.s.canvas} (${w3.ms} ms)`);
		check(w3.s.parentLog.some((e) => e.what.startsWith('got godot:exit-fullscreen')), '3: parent got godot:exit-fullscreen');
		check(after && after.mode === FULLSCREEN, `3: mode stays FULLSCREEN until the parent answers (right after the call: ${after && after.mode})`);
		await sleep(300);
		consistent(await p.snap(), '3 windowed again');

		// 8: the game's own Fullscreen button, through godot:request-fullscreen.
		const n8 = (await p.snap()).parentLog.length;
		const t8 = Date.now();
		await p.clickCanvas(120, 45);
		const w8 = await p.waitFor((s) => s.godot.mode === FULLSCREEN && eq(s.canvas, fullSize(s)));
		const log8 = w8.s.parentLog.slice(n8).map((e) => e.what);
		check(w8.ok && w8.s.parentFs === 'game' && w8.s.ownFs === null,
			`8 in-game Fullscreen -> godot:request-fullscreen: parent fs ${w8.s.parentFs}, game fs ${w8.s.ownFs}, mode ${w8.s.godot.mode} after ${Date.now() - t8} ms; parent: ${log8.join('; ')}`);
		check(log8.includes('got godot:request-fullscreen from http://127.0.0.1:8702') && log8.includes('request ok'), '8: parent requested the iframe fullscreen and it was granted (activation from the iframe click)');
		await shot(p, '8_request_fullscreen');
		await p.clickCanvas(340, 45);
		const w8b = await p.waitFor((s) => s.parentFs === null && s.godot.mode === WINDOWED && eq(s.canvas, s0.canvas));
		check(w8b.ok, `8: in-game Windowed -> godot:exit-fullscreen -> windowed (${w8b.ms} ms)`);
		note(`main console: ${p.consoleLines.length} errors/warnings${p.consoleLines.length ? ': ' + p.consoleLines.slice(0, 5).join(' || ') : ''}`);
		await p.context.close();
	},

	// 7 + the request fallback: the parent ignores godot:request-fullscreen, so the canvas path runs after 1 s; the
	// parent then reports its iframe fullscreen (the echo), which must not be handled twice.
	async echo() {
		const p = await openParent('main', '&norequest');
		await p.waitReady();
		await sleep(1000);
		const s0 = await p.snap();
		const n0 = s0.parentLog.length;
		const t0 = Date.now();
		await p.clickCanvas(120, 45);
		const w = await p.waitFor((s) => s.godot.mode === FULLSCREEN && s.ownFs === 'canvas', 6000);
		check(w.ok, `7 in-game Fullscreen (parent ignores the request): canvas fullscreen after ${Date.now() - t0} ms (game fs ${w.s.ownFs}, parent fs ${w.s.parentFs})`);
		await sleep(1500);
		const s1 = await p.snap();
		const plog = s1.parentLog.slice(n0).map((e) => e.what);
		check(s1.godot.mode === FULLSCREEN && s1.godot.mode_changes === s0.godot.mode_changes + 1,
			`7: one mode change for the canvas fullscreen and its echo (${s0.godot.mode_changes} -> ${s1.godot.mode_changes}); parent: ${plog.join('; ')}`);
		check(plog.includes('send on=true'), '7: the parent did echo {on:true}');
		check(eq(s1.canvas, fullSize(s1)) && eq(s1.godot.window, s1.canvas), `7: canvas ${s1.canvas} = screen ${fullSize(s1)}`);
		await shot(p, '7_canvas_fullscreen');
		await p.clickCanvas(340, 45);
		const w2 = await p.waitFor((s) => s.godot.mode === WINDOWED && s.ownFs === null && s.parentFs === null && eq(s.canvas, s0.canvas));
		await sleep(1500);
		const s2 = await p.snap();
		const plog2 = s2.parentLog.slice(n0).map((e) => e.what);
		check(w2.ok && s2.godot.mode === WINDOWED && s2.godot.mode_changes === s0.godot.mode_changes + 2,
			`7: in-game Windowed leaves the canvas fullscreen; mode changes ${s0.godot.mode_changes} -> ${s2.godot.mode_changes} (2 expected)`);
		check(!plog2.some((e) => e.startsWith('got godot:exit-fullscreen')), '7: no godot:exit-fullscreen for the canvas path');
		consistent(s2, '7 windowed again');
		await p.context.close();
	},

	// 7 with a parent that does not speak the protocol at all: exactly as before (canvas fullscreen at once).
	async silent() {
		const p = await openParent('main', '&silent');
		await p.waitReady();
		await sleep(1000);
		const s0 = await p.snap();
		const t0 = Date.now();
		await p.clickCanvas(120, 45);
		const w = await p.waitFor((s) => s.godot.mode === FULLSCREEN && s.ownFs === 'canvas', 6000);
		check(w.ok && Date.now() - t0 < 1000, `7 silent parent: canvas fullscreen without the request detour (${Date.now() - t0} ms)`);
		await p.clickCanvas(340, 45);
		const w2 = await p.waitFor((s) => s.godot.mode === WINDOWED && s.parentFs === null && eq(s.canvas, s0.canvas));
		check(w2.ok, 'silent: back to windowed');
		// The parent fullscreens the iframe but says nothing: the game stays windowed, scaled by the shell (as before).
		await p.page.click('#game-fullscreen');
		await sleep(2000);
		const s3 = await p.snap();
		check(s3.parentFs === 'game' && s3.godot.mode === WINDOWED && eq(s3.canvas, s0.canvas), `silent: iframe fullscreen without messages leaves the game windowed (canvas ${s3.canvas})`);
		consistent(s3, 'silent scaled');
		await exitParent(p);
		await p.context.close();
	},

	// 4: the parent goes fullscreen on the start screen, and (b) during loading.
	async start() {
		for (const when of ['start-screen', 'loading']) {
			const p = await openParent('start');
			await p.frame().waitForSelector('#status-start', { state: 'visible', timeout: 30000 });
			await sleep(500);
			if (when === 'start-screen') {
				await p.page.click('#game-fullscreen');
				await sleep(1500);
				check(await p.page.evaluate(() => document.fullscreenElement && document.fullscreenElement.id === 'game'), '4a: parent fullscreen on the start screen');
				await p.frame().click('#status-start');
			} else {
				await p.frame().click('#status-start');
				await sleep(300);
				await p.page.click('#game-fullscreen');
				await sleep(200);
				const loading = await p.frame().evaluate(() => !!document.getElementById('status') && !(window.__log || []).length);
				check(loading, `4b: parent fullscreen while loading (status overlay still up: ${loading})`);
			}
			await p.waitReady();
			const w = await p.waitFor((s) => s.godot.mode === FULLSCREEN && eq(s.canvas, fullSize(s)) && eq(s.godot.window, s.canvas));
			const first = w.s.log[0];
			check(w.ok, `4 ${when}: game comes up fullscreen, canvas ${w.s.canvas} (first log mode ${first.mode}, ${w.ms} ms after ready)`);
			if (w.ok) consistent(w.s, `4 ${when}`);
			await shot(p, `4_${when}`);
			await exitParent(p);
			check((await p.waitFor((s) => s.godot.mode === WINDOWED && eq(s.canvas, [1024, 640].map((v) => Math.floor(v)) ) || (s.godot.mode === WINDOWED))).ok, `4 ${when}: exits to windowed`);
			await p.context.close();
		}
	},

	// 5: allowed_origins set to another origin: the parent's messages are ignored.
	async deny() {
		const p = await openParent('deny');
		await p.waitReady();
		await sleep(1000);
		const s0 = await p.snap();
		await p.page.click('#game-fullscreen');
		await sleep(2500);
		const s1 = await p.snap();
		check(s1.parentFs === 'game' && s1.godot.mode === WINDOWED && eq(s1.canvas, s0.canvas),
			`5 allowed_origins=https://lms.example.edu: parent on localhost:8701 ignored (mode ${s1.godot.mode}, canvas ${s1.canvas})`);
		await exitParent(p);
		await p.context.close();
	},

	// Positive control for allowed_origins: the parent's own origin listed.
	async allow() {
		const p = await openParent('allow');
		await p.waitReady();
		await sleep(1000);
		const s0 = await p.snap();
		await p.page.click('#game-fullscreen');
		const w = await p.waitFor((s) => s.godot.mode === FULLSCREEN && eq(s.canvas, fullSize(s)));
		check(w.ok, `5 allowed_origins=http://localhost:8701: accepted, mode ${w.s.godot.mode}, canvas ${w.s.canvas}`);
		await exitParent(p);
		check((await p.waitFor((s) => s.godot.mode === WINDOWED && eq(s.canvas, s0.canvas))).ok, 'allow: back to windowed');
		await p.context.close();
	},

	// 6: option off: no 'message' listener from the engine, and no reaction to the messages.
	async off() {
		const p = await openParent('off');
		await p.waitReady();
		await sleep(1000);
		const s0 = await p.snap();
		const listeners = await p.frame().evaluate(() => window.__msgListeners);
		check(listeners.length === 0, `6 option off: no window 'message' listener added (${listeners.length}${listeners.length ? ': ' + listeners.join(' ;; ') : ''})`);
		if (BROWSER === 'chrome') {
			try {
				const cdp = await p.page.context().newCDPSession(p.frame());
				const { result } = await cdp.send('Runtime.evaluate', { expression: 'window' });
				const { listeners: ls } = await cdp.send('DOMDebugger.getEventListeners', { objectId: result.objectId });
				const msg = ls.filter((l) => l.type === 'message');
				check(msg.length === 0, `6: DevTools getEventListeners(window) 'message': ${msg.length} (all types: ${[...new Set(ls.map((l) => l.type))].join(',')})`);
			} catch (e) {
				note(`6: CDP getEventListeners unavailable: ${e.message}`);
			}
		}
		check(!s0.parentLog.some((e) => e.what.startsWith('got godot:ready')), '6: no godot:ready posted');
		await p.page.click('#game-fullscreen');
		await sleep(2500);
		const s1 = await p.snap();
		check(s1.parentFs === 'game' && s1.godot.mode === WINDOWED && eq(s1.canvas, s0.canvas), `6: parent fullscreen leaves the game windowed (as before; canvas ${s1.canvas})`);
		consistent(s1, '6 scaled by the shell');
		await shot(p, '6_off_parent_fullscreen');
		await exitParent(p);
		await sleep(1000);
		consistent(await p.snap(), '6 back');
		// The game's own button still works as before.
		await p.clickCanvas(120, 45);
		const w = await p.waitFor((s) => s.godot.mode === FULLSCREEN && s.ownFs === 'canvas', 3000);
		check(w.ok && w.ms < 1000, `6: in-game Fullscreen -> canvas fullscreen (${w.ms} ms)`);
		await p.clickCanvas(340, 45);
		check((await p.waitFor((s) => s.godot.mode === WINDOWED && s.parentFs === null)).ok, '6: in-game Windowed -> windowed');
		await p.context.close();
	},
};

try {
	if (await adapterCheck()) {
		for (const [name, fn] of Object.entries(scenarios)) {
			if (ONLY && !ONLY.includes(name)) continue;
			console.log(`--- ${BROWSER} ${name}`);
			try {
				await fn();
			} catch (e) {
				check(false, `${name}: exception ${e.message.split('\n')[0]}`);
			}
		}
	}
} finally {
	await writeFile(path.join(OUT, `${BROWSER}.json`), JSON.stringify(results, null, 1));
	await browser.close();
	s1.close(); s2.close();
}
console.log(failures ? `FAIL (${failures})` : 'ALL PASS');
process.exit(failures ? 1 : 0);
