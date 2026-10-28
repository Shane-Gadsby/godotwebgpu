// Start-screen test, adapted from webgpu_notes/work/inputs/camera-sim-2026-10-28/test-scripts/start_test.mjs.
// Parent on localhost:8701 (a tall page, the game iframe partly below the fold), game on 127.0.0.1:8702.
// usage: node start_test.mjs <chrome|firefox> <game_dir> <out_dir> <mode: start|auto> [--direct] [--key Enter|Space]
//   start: the page must open on the Start button; checks nothing of the engine ran, button styles, press, game up.
//   auto:  no button; the game must start on its own.
//   --direct: load the game page itself, not in the iframe.
import { createServer } from 'http';
import { readFile, mkdir } from 'fs/promises';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium, firefox } = require('playwright');
const { FIREFOX_WEBGPU_PREFS } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu-51/webgpu_tests/scene_smoketest/firefox_prefs.mjs');

const args = process.argv.slice(2);
const [BROWSER, GAME, OUT, MODE] = args;
const DIRECT = args.includes('--direct');
const KEY = args.includes('--key') ? args[args.indexOf('--key') + 1] : null;
const PARENT = '/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w51/parent';
await mkdir(OUT, { recursive: true });
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.pck': 'application/octet-stream' };
const requests = [];
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${what}`); if (!ok) failures++; };
function serve(dir, host, port) {
	return new Promise((resolve) => {
		const s = createServer(async (req, res) => {
			const name = decodeURIComponent(req.url.split('?')[0]);
			if (name === '/__blank') { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<!doctype html><title>blank</title>'); return; }
			if (port === 8702) requests.push(name);
			const p = path.join(dir, name);
			let data;
			try { data = await readFile(p); } catch (e) { res.writeHead(404); res.end(); return; }
			res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream', 'Content-Length': data.length });
			res.end(data);
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
const page = await browser.newPage({ viewport: { width: 1100, height: 800 } });
const consoleErrors = [];
page.on('console', (m) => { if (m.type() === 'error') consoleErrors.push(m.text().slice(0, 300)); });
page.on('pageerror', (e) => consoleErrors.push(`pageerror: ${e.message}`));

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
consoleErrors.length = 0;

const t0 = Date.now();
const T = () => `${((Date.now() - t0) / 1000).toFixed(1)}s`;
let frame;
if (DIRECT) {
	await page.goto('http://127.0.0.1:8702/index.html');
	frame = () => page.mainFrame();
} else {
	await page.goto('http://localhost:8701/index.html');
	frame = () => page.frames().find((f) => f.url().startsWith('http://127.0.0.1:8702'));
	while (!frame()) await page.waitForTimeout(100);
}
const engineState = () => frame().evaluate(() => ({
	// `engine` is a global in full-size.html; webgpu-full-size.html keeps it private.
	engineRunning: (typeof engine !== 'undefined') ? !!(engine && engine.rtenv) : 'n/a',
	canvasCssWidth: document.getElementById('canvas').style.width,
	statusShown: !!document.getElementById('status'),
}));

if (MODE === 'start') {
	await frame().waitForSelector('#status-start', { state: 'visible', timeout: 20000 });
	await page.waitForTimeout(2000);
	const st = await engineState();
	console.log(T(), 'start screen', JSON.stringify(st));
	check(st.engineRunning === false || st.engineRunning === 'n/a', 'engine.rtenv unset before the press');
	check(st.canvasCssWidth === '', 'canvas has no inline width before the press');
	const before = [...new Set(requests)].sort();
	console.log('requests before press:', JSON.stringify(before));
	check(before.every((r) => ['/index.html', '/index.js', '/index.png', '/favicon.ico'].includes(r)), 'only the page, index.js and the splash requested before the press');
	if (!DIRECT) {
		const scroll = await page.evaluate(() => window.scrollY);
		check(scroll === 0, `parent page not scrolled on load (scrollY ${scroll})`);
	}
	const focused = await frame().evaluate(() => document.activeElement && document.activeElement.id);
	check(focused !== 'status-start', `button not focused on load (active: ${focused || 'none'})`);
	const btn = await frame().$('#status-start');
	const style = () => btn.evaluate((b) => {
		const c = getComputedStyle(b);
		return { bg: c.backgroundColor, color: c.color, cursor: c.cursor, border: `${c.borderTopWidth} ${c.borderTopStyle} ${c.borderTopColor}`, radius: c.borderTopLeftRadius, font: `${c.fontSize} ${c.fontFamily}`, padding: `${c.paddingTop} ${c.paddingLeft}`, outline: `${c.outlineStyle} ${c.outlineWidth} ${c.outlineColor}`, text: b.textContent, aria: b.getAttribute('aria-label'), type: b.type };
	});
	const idle = await style();
	console.log('idle', JSON.stringify(idle));
	await page.screenshot({ path: `${OUT}/1_start.png` });
	await page.mouse.move(0, 0);
	await btn.hover();
	await page.waitForTimeout(300);
	const hover = await style();
	console.log('hover', JSON.stringify(hover));
	await btn.screenshot({ path: `${OUT}/2_hover.png` });
	check(hover.bg !== idle.bg, 'hover changes the background');
	check(idle.cursor === 'pointer', 'cursor: pointer');
	if (KEY) {
		// Keyboard path: move the pointer away, Tab to the button, check the focus ring, press KEY.
		await page.mouse.move(0, 0);
		await frame().evaluate(() => document.activeElement && document.activeElement.blur && document.activeElement.blur());
		let tries = 0;
		while ((await frame().evaluate(() => document.activeElement && document.activeElement.id)) !== 'status-start' && tries++ < 6) {
			await page.keyboard.press('Tab');
		}
		const focusedStyle = await style();
		console.log('focused', JSON.stringify(focusedStyle), 'tabs', tries);
		check(focusedStyle.outline.startsWith('solid'), 'keyboard focus shows a ring');
		await btn.screenshot({ path: `${OUT}/3_focus.png` });
		// Pressed look while the key is held (Space activates on release; Enter on press).
		if (KEY === 'Space') {
			await page.keyboard.down('Space');
			await page.waitForTimeout(200);
			const pressed = await style();
			console.log('pressed (Space held)', JSON.stringify(pressed));
			check(pressed.bg !== idle.bg, 'Space held shows a pressed background');
			await page.keyboard.up('Space');
		} else {
			await page.keyboard.press('Enter');
		}
	} else {
		// Element screenshots scroll the page; one between mouse down and up would make the
		// release land elsewhere and swallow the click, so clip page screenshots instead.
		await btn.scrollIntoViewIfNeeded();
		const box = await btn.boundingBox();
		await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
		await page.mouse.down();
		await page.waitForTimeout(200);
		const pressed = await style();
		console.log('pressed', JSON.stringify(pressed));
		check(pressed.bg !== idle.bg && pressed.bg !== hover.bg, 'pressed background differs from idle and hover');
		// No screenshot while the button is held: in headed Chrome it delayed the release, and
		// with it the click, by over a minute. The pressed look is checked from the computed style.
		await page.mouse.up();
	}
	console.log(T(), 'pressed Start');
	await page.waitForTimeout(100);
	check(await frame().evaluate(() => document.getElementById('status-start').disabled), 'the press reached the button (now disabled)');
	await page.waitForTimeout(500);
	const after = await engineState();
	console.log(T(), '0.5 s after press', JSON.stringify(after));
	const label = await frame().evaluate(() => { const l = document.getElementById('status-label'); return l ? l.textContent : 'n/a'; });
	console.log('loading label after press:', label);
	await page.screenshot({ path: `${OUT}/4_loading.png` });
}
await frame().waitForFunction(() => !document.getElementById('status'), null, { timeout: 120000, polling: 100 });
console.log(T(), 'game up');
await page.waitForTimeout(3000);
const running = await engineState();
console.log(T(), 'running', JSON.stringify(running));
check(running.engineRunning === true || running.engineRunning === 'n/a', 'engine running');
check(running.canvasCssWidth !== '', 'canvas sized by the engine');
await page.screenshot({ path: `${OUT}/5_running.png` });
const canvasShot = await (await frame().$('#canvas')).screenshot({ path: `${OUT}/canvas.png` });
console.log('requests total:', JSON.stringify([...new Set(requests)].sort()));
// Known and unrelated: the favicon 404 (the test server has none) and, inside the iframe, the engine's
// keep_screen_on wake lock, which the parent's iframe does not allow (no allow="screen-wake-lock").
const known = (e) => /status of 404/.test(e) || /screen-wake-lock is not allowed/.test(e) || /NotAllowedError.*[Ww]ache/.test(e);
console.log('known console errors ignored:', JSON.stringify(consoleErrors.filter(known)));
const errors = consoleErrors.filter((e) => !known(e));
check(errors.length === 0, `no other console errors (${JSON.stringify(errors)})`);
await browser.close();
s1.close(); s2.close();
console.log(failures ? `RESULT FAIL (${failures})` : 'RESULT PASS');
process.exit(failures ? 1 : 0);
