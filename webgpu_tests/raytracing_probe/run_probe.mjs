#!/usr/bin/env node
// Serves the probe over 127.0.0.1 (WebGPU needs a secure context), drives it in a browser, and
// prints the ray throughput table. Default: the system Chrome, headed, on the real GPU, launched
// exactly as every other real-GPU tier does (webgpu_notes/TASKS.md §3 "Measurement traps"). With
// CI=1 or --software it uses Playwright's bundled Chromium on swiftshader at a small resolution,
// which checks that the kernels still run, not what the hardware can do.
//
//   node run_probe.mjs [--tris 100000,1000000] [--w 1920] [--h 1080] [--iters 20]
//                      [--software] [--shot out.png] [--url-only] [--keep-open]
//
// --url-only serves and prints the URL without launching anything, for opening the probe in a
// browser you already have up (see the memory rule on attaching to an open tab); the result then
// shows on the page and in its console as a [PROBE-RESULT] line.
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
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/?tris=${tris}&w=${w}&h=${h}&iters=${iters}`;

if (flag('url-only')) {
	console.log(`Serving the probe at ${url}\nOpen it in a WebGPU-enabled browser; Ctrl-C to stop.`);
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

if (!result.ok) {
	console.error(`\nPROBE FAILED: ${result.error || (result.errors || []).join('; ')}`);
	if (!flag('keep-open')) {
		process.exit(1);
	}
}
const a = result.adapter || {};
console.log(`\nadapter: ${a.vendor} / ${a.architecture} ${a.description}${a.isFallbackAdapter ? '  (FALLBACK ADAPTER -- software)' : ''}`);
console.log(`resolution ${result.resolution.join('x')}, ${result.iters} timed dispatches per kernel, time = median GPU timestamp (wall clock when no timestamp-query)\n`);
console.log('| triangles | BVH build | primary ms / Mrays | shadow ms / Mrays | bounce ms / Mrays | 1 spp frame |');
console.log('|---|---|---|---|---|---|');
for (const s of result.scenes) {
	const f = (k) => `${s[k].ms.toFixed(2)} / ${s[k].mrays_per_s.toFixed(0)}`;
	console.log(`| ${s.triangles.toLocaleString()} | ${s.build_ms.toFixed(0)} ms | ${f('primary')} | ${f('shadow')} | ${f('bounce')} | ${s.frame_ms_1spp.toFixed(2)} ms |`);
}
if (!software && a.isFallbackAdapter) {
	console.error('\nThe real-GPU run landed on the fallback adapter; these numbers are not the hardware\'s.');
	process.exit(2);
}
