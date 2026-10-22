#!/usr/bin/env node
// Serves the probe over 127.0.0.1 (WebGPU needs a secure context), drives it in a browser, and
// prints the ray throughput table. Default: the system Chrome, headed, on the real GPU, launched
// exactly as every other real-GPU tier does (webgpu_notes/TASKS.md §3 "Measurement traps"). With
// CI=1 or --software it uses Playwright's bundled Chromium on swiftshader at a small resolution,
// which checks that the kernels still run, not what the hardware can do.
//
//   node run_probe.mjs [--tris 100000,1000000] [--w 1920] [--h 1080] [--iters 20] [--verify 64]
//                      [--software] [--shot out.png] [--url-only] [--port 8099] [--keep-open]
//
// --url-only serves and prints the URL without launching anything, for opening the probe in a
// browser you already have up, or on the phone: `--port 8099` plus `adb reverse tcp:8099 tcp:8099`
// makes http://localhost:8099/ on the phone a secure context (the perf_guard/android recipe); the
// result shows on the page and in its console as a [PROBE-RESULT] line.
//
// Exit codes: 0 all checks passed, 1 a check failed or the probe errored, 2 a real-GPU run landed
// on the fallback adapter. The checks (traversal vs a brute-force CPU trace on a grid of primary
// rays, plausible hit/occlusion fractions for the fixed camera, BVH depth within the stack) make the
// --software run a correctness test; only a real GPU gives throughput numbers that mean anything.
import { createServer } from 'http';
import { readFileSync, mkdirSync, writeFileSync, existsSync } from 'fs';
import { dirname, join, extname } from 'path';
import { fileURLToPath } from 'url';
import { mkdtempSync } from 'fs';
import { tmpdir } from 'os';

const __dirname = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (name, def) => {
	const i = args.indexOf(`--${name}`);
	return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
};
const flag = (name) => args.includes(`--${name}`);
const software = flag('software') || !!process.env.CI;
const tris = opt('tris', software ? '20000' : '100000,1000000');
const w = opt('w', software ? '320' : '1920');
const h = opt('h', software ? '180' : '1080');
const iters = opt('iters', software ? '3' : '20');
const shot = opt('shot', null);
const verify = opt('verify', '64');
const port = +opt('port', '0');

const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.json': 'application/json' };
const server = createServer((req, res) => {
	const path = req.url.split('?')[0];
	const file = join(__dirname, path === '/' ? 'index.html' : path.replace(/^\/+/, ''));
	if (!file.startsWith(__dirname) || !existsSync(file)) {
		res.writeHead(404);
		res.end('not found');
		return;
	}
	res.writeHead(200, { 'Content-Type': types[extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
	res.end(readFileSync(file));
});
await new Promise((r) => server.listen(port, '127.0.0.1', r));
const query = `?tris=${tris}&w=${w}&h=${h}&iters=${iters}&verify=${verify}`;
const url = `http://127.0.0.1:${server.address().port}/${query}`;

if (flag('url-only')) {
	console.log(`Serving the probe at ${url}\nOpen it in a WebGPU-enabled browser; Ctrl-C to stop.`);
	console.log(`Phone: adb reverse tcp:${server.address().port} tcp:${server.address().port}, then open http://localhost:${server.address().port}/${query} in Chrome on the device`);
	console.log('       (use --w 1280 --h 720 --tris 100000 --iters 10 there; the result is on the page itself).');
	await new Promise(() => {});
}

const pw = await import(join(__dirname, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
let browser;
let context;
if (software) {
	browser = await pw.chromium.launch({
		headless: true,
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer', '--use-angle=swiftshader', '--enable-gpu', '--ignore-gpu-blocklist', '--no-sandbox'],
	});
	context = await browser.newContext();
} else {
	context = await pw.chromium.launchPersistentContext(mkdtempSync(join(tmpdir(), 'rtprobe-')), {
		executablePath: '/usr/bin/google-chrome-stable',
		ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
		headless: false,
		viewport: { width: 1280, height: 900 },
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	});
}
const page = await context.newPage();
page.on('console', (m) => {
	const t = m.text();
	if (t.startsWith('[probe]')) {
		console.log(t.slice(8));
	}
});
page.on('pageerror', (e) => console.error('page error:', e.message));
await page.goto(url);
await page.waitForFunction(() => window.__probeDone === true, null, { timeout: 15 * 60 * 1000 });
const result = await page.evaluate(() => window.__probeResult);
if (shot) {
	await page.screenshot({ path: shot });
	console.log(`screenshot: ${shot}`);
}
if (!flag('keep-open')) {
	await context.close();
	if (browser) {
		await browser.close();
	}
}
server.close();

mkdirSync(join(__dirname, 'results'), { recursive: true });
const stamp = new Date().toISOString().replace(/[:.]/g, '-');
writeFileSync(join(__dirname, 'results', `${stamp}.json`), JSON.stringify(result, null, 2));
writeFileSync(join(__dirname, 'results', 'latest.json'), JSON.stringify(result, null, 2));

if (result.error) {
	console.error(`\nPROBE ERROR: ${result.error}`);
	process.exit(1);
}
const a = result.adapter || {};
console.log(`\nadapter: ${a.vendor} / ${a.architecture} ${a.description}${a.isFallbackAdapter ? '  (FALLBACK ADAPTER -- software)' : ''}`);
console.log(`resolution ${result.resolution.join('x')}, ${result.iters} timed dispatches per kernel, time = median GPU timestamp (wall clock when no timestamp-query)\n`);
console.log('| triangles | BVH build | primary ms / Mrays | shadow ms / Mrays | bounce ms / Mrays | 1 spp frame | traversal vs brute force |');
console.log('|---|---|---|---|---|---|---|');
for (const s of result.scenes) {
	const f = (k) => `${s[k].ms.toFixed(2)} / ${s[k].mrays_per_s.toFixed(0)}`;
	const v = s.verify ? `${s.verify.samples - s.verify.mismatches}/${s.verify.samples} rays agree` : 'not checked';
	console.log(`| ${s.triangles.toLocaleString()} | ${s.build_ms.toFixed(0)} ms | ${f('primary')} | ${f('shadow')} | ${f('bounce')} | ${s.frame_ms_1spp.toFixed(2)} ms | ${v} |`);
}
const passed = (result.checks || []).filter((c) => c.pass).length;
console.log(`\nchecks: ${passed}/${(result.checks || []).length} passed${result.ok ? '' : '  -- FAILED'}`);
for (const c of result.checks || []) {
	if (!c.pass) {
		console.log(`  FAIL [${c.scene}] ${c.name}: ${c.detail}`);
	}
}
if (!software && a.isFallbackAdapter) {
	console.error('\nThe real-GPU run landed on the fallback adapter; these numbers are not the hardware\'s.');
	process.exit(2);
}
if (!result.ok) {
	process.exit(1);
}
