// node trace_run.mjs --dir <export> [--query q] [--browser chrome|firefox] [--timeout s] [--out f.json] [--size WxH]
// Loads an export with gpu_trace_init.js injected; waits for window.__cap_done or the timeout; writes console lines,
// console error/warning counts and the GPU buffer trace.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
const here = dirname(fileURLToPath(import.meta.url));
const REPO = '/mnt/109313D2109313D2/godot-editors/godotwebgpu';
const { chromium, firefox } = await import(join(REPO, 'webgpu_tests/scene_smoketest/node_modules/playwright/index.mjs'));
const { FIREFOX_WEBGPU_PREFS } = await import(join(REPO, 'webgpu_tests/scene_smoketest/firefox_prefs.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const QUERY = arg('query', '');
const OUT = arg('out', 'out.json');
const BROWSER = arg('browser', 'chrome');
const [W, H] = arg('size', '1920x1080').split('x').map(Number);
const TIMEOUT = +arg('timeout', 120) * 1000;
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.bin': 'application/octet-stream' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]);
	if (p === '/__probe.html') { r.writeHead(200, { 'Content-Type': 'text/html' }); r.end('<html><body>probe</body></html>'); return; }
	const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;
const browser = BROWSER === 'firefox' ? await firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS }) : await chromium.launch({
	executablePath: '/usr/bin/google-chrome-stable', headless: false,
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] });
let code = 0;
const res = { query: QUERY, browser: BROWSER, lines: [], counts: { error: 0, warning: 0 }, msgs: [], done: false };
try {
	const probe = await browser.newPage();
	await probe.goto(base + '__probe.html');
	const ad = await probe.evaluate(async () => { const a = navigator.gpu ? await navigator.gpu.requestAdapter() : null; return a ? { vendor: a.info.vendor, architecture: a.info.architecture, fallback: a.info.isFallbackAdapter, maxSampled: a.limits.maxSampledTexturesPerShaderStage } : null; });
	await probe.close();
	res.adapter = ad;
	console.log('adapter', JSON.stringify(ad));
	const okAd = ad && !ad.fallback && (BROWSER === 'firefox' || (`${ad.vendor}/${ad.architecture}` === 'nvidia/lovelace' && ad.maxSampled >= 48));
	if (!okAd) { console.log('ADAPTER REFUSED'); process.exitCode = 3; throw new Error('adapter'); }
	const page = await browser.newPage({ viewport: { width: W, height: H } });
	await page.addInitScript({ path: join(here, 'gpu_trace_init.js') });
	page.on('console', (m) => {
		const t = m.text();
		const ty = m.type();
		if (t.startsWith('W66')) { res.lines.push(t.slice(0, 3000)); console.log(t.slice(0, 600)); if (t.startsWith('W66DONE')) page.evaluate(() => { window.__cap_done = true; }).catch(() => {}); return; }
		if (t.startsWith('[cap] ')) { try { res.lines.push(JSON.parse(t.slice(6))); } catch { res.lines.push(t); } console.log(t.slice(0, 300)); return; }
		if (ty === 'error' || ty === 'warning') {
			res.counts[ty]++;
			if (res.msgs.length < 60) res.msgs.push(`[${ty}] ${t.split('\n')[0].slice(0, 300)}`);
		}
	});
	page.on('pageerror', (e) => { res.counts.error++; res.msgs.push('[pageerror] ' + e.message); });
	await page.goto(`${base}index.html?${QUERY}`);
	try {
		await page.waitForFunction(() => window.__cap_done === true, null, { timeout: TIMEOUT, polling: 500 });
		res.done = true;
	} catch { console.log('TIMEOUT'); code = 4; }
	const ev = await page.evaluate(() => { const e = window.__gpuev; const lat = e.mapLatency.slice().sort((a, b) => a - b); return Object.assign({}, e, { mapLatency: undefined, latMedian: lat[lat.length >> 1], latMax: lat[lat.length - 1] }); });
	res.gpu = ev;
	console.log('gpu', JSON.stringify(Object.assign({}, ev, { bad: ev.bad.slice(0, 2) })).slice(0, 1500));
	console.log('counts', JSON.stringify(res.counts));
	for (const m of res.msgs.slice(0, 12)) console.log(' ', m);
} catch (e) {
	if (e.message !== 'adapter') { console.error('FAILED:', e.message); code = 1; }
} finally {
	writeFileSync(OUT, JSON.stringify(res, null, 1));
	await browser.close();
	server.close();
}
process.exit(process.exitCode || code);
