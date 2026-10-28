// Task 75 web runner: serve an export, open it in the mandated browser launch, assert the adapter first, collect the
// page's [dr] lines and the engine's "Dynamic 3D resolution" verbose lines.
//   node run_web.mjs --dir <export> --query "mode=ctrl&iters=4000" --out x.json [--browser firefox] [--size 1920x1080]
//        [--timeout 200] [--withhold] (device without timestamp-query) [--reload] (load twice in one browser context:
//        the remember round trip) [--click Start] (press a start button) [--duration 60] (no __dr_done: run this long)
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync } from 'fs';
import { join, extname, resolve } from 'path';
const REPO = '/mnt/109313D2109313D2/godot-editors/godotwebgpu'; // playwright's node_modules (read-only use)
const { FIREFOX_WEBGPU_PREFS } = await import(join(REPO, 'webgpu_tests/scene_smoketest/firefox_prefs.mjs'));
const { chromium, firefox } = await import(join(REPO, 'webgpu_tests/scene_smoketest/node_modules/playwright/index.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const QUERY = arg('query', '');
const OUT = arg('out', 'out.json');
const BROWSER = arg('browser', 'chrome');
const [W, H] = arg('size', '1920x1080').split('x').map(Number);
const TIMEOUT = +arg('timeout', 200) * 1000;
const WITHHOLD = process.argv.includes('--withhold');
const RELOAD = process.argv.includes('--reload');
const CLICK = arg('click', '');
const DURATION = +arg('duration', 0) * 1000;
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.cfg': 'text/plain' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const browser = BROWSER === 'firefox'
	? await firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS })
	: await chromium.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false,
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
		ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] });
const context = await browser.newContext({ viewport: { width: W, height: H } });
const runs = [];
let adapterFail = null;

async function runOnce(n) {
	const page = await context.newPage();
	const run = { n, adapter: null, lines: [], engine: [], errors: [], completed: false, canvas: null };
	runs.push(run);
	page.on('console', (m) => {
		const t = m.text();
		if (t.startsWith('[dr] ')) { const j = JSON.parse(t.slice(5)); run.lines.push(j); console.log(`#${n}`, t.slice(0, 260)); }
		else if (t.startsWith('[drjs] ')) { console.log(`#${n}`, t); if (t.includes('adapter')) run.adapter = t; }
		else if (t.includes('Dynamic 3D resolution')) { run.engine.push([Date.now(), t]); console.log(`#${n}  eng:`, t.slice(0, 220)); }
		if (m.type() === 'error' || /WebGPU uncaptured error|validation/i.test(t)) run.errors.push(t.slice(0, 400));
	});
	page.on('pageerror', (e) => run.errors.push('pageerror: ' + String(e).slice(0, 400)));
	await page.addInitScript((withhold) => {
		const ad = GPU.prototype.requestAdapter;
		GPU.prototype.requestAdapter = async function (...a) {
			const ada = await ad.apply(this, a);
			if (ada) {
				const i = ada.info || {};
				console.log(`[drjs] adapter ${i.vendor}/${i.architecture} fallback=${i.isFallbackAdapter} maxSampledTexturesPerShaderStage=${ada.limits.maxSampledTexturesPerShaderStage} timestamp-query=${ada.features.has('timestamp-query')}`);
			} else console.log('[drjs] adapter null');
			return ada;
		};
		const rd = GPUAdapter.prototype.requestDevice;
		GPUAdapter.prototype.requestDevice = async function (desc = {}) {
			if (!withhold) return rd.call(this, desc);
			const d = { ...desc, requiredFeatures: (desc.requiredFeatures || []).filter((f) => f !== 'timestamp-query') };
			const dev = await rd.call(this, d);
			console.log(`[drjs] withheld: device timestamp-query=${dev.features.has('timestamp-query')}`);
			return dev;
		};
	}, WITHHOLD);
	await page.goto(`http://127.0.0.1:${server.address().port}/?${QUERY}`);
	const t0 = Date.now();
	// A start button (camera-sim) comes before the engine asks for an adapter.
	if (CLICK) {
		const btn = page.getByText(CLICK, { exact: true });
		await btn.waitFor({ timeout: 120000 });
		await btn.click();
		run.clicked = Date.now() - t0;
		console.log(`#${n} clicked ${CLICK} at ${run.clicked} ms`);
	}
	// The adapter first: a wrong adapter makes every number below meaningless (README browser rule).
	while (!run.adapter && Date.now() - t0 < 30000) await new Promise((r) => setTimeout(r, 200));
	const a = run.adapter || '';
	if (!a || /fallback=true/.test(a) || / null/.test(a) || (BROWSER === 'chrome' && (!/nvidia/i.test(a) || !/maxSampledTexturesPerShaderStage=(4[89]|[5-9]\d|\d{3})/.test(a)))) {
		adapterFail = a || 'no adapter line';
		console.log('ADAPTER CHECK FAILED:', adapterFail);
		return false;
	}
	const started = Date.now();
	while (Date.now() - t0 < TIMEOUT) {
		await new Promise((r) => setTimeout(r, 1000));
		if (DURATION && Date.now() - started >= DURATION) { run.completed = true; break; }
		if (!DURATION && await page.evaluate(() => !!window.__dr_done).catch(() => false)) { run.completed = true; break; }
	}
	run.canvas = await page.evaluate(() => { const c = document.querySelector('canvas'); return c ? [c.width, c.height] : null; }).catch(() => null);
	await page.close();
	return true;
}

let ok = await runOnce(1);
if (ok && RELOAD) ok = await runOnce(2);
writeFileSync(OUT, JSON.stringify({ query: QUERY, browser: BROWSER, withhold: WITHHOLD, adapterFail, runs }, null, 1));
for (const r of runs) { console.log(`#${r.n} canvas=${r.canvas} completed=${r.completed} console errors: ${r.errors.length}`); for (const e of r.errors.slice(0, 8)) console.log('  ERR', e); }
await browser.close(); server.close();
if (adapterFail) process.exit(3);
if (runs.some((r) => !r.completed)) { console.log('TIMEOUT: page never finished'); process.exit(2); }
console.log('wrote', OUT);
