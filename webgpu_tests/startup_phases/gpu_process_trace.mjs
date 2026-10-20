/**
 * Where the startup goes, on both sides of the GPU process boundary (TASKS.md §2.4).
 *
 * profile_phases.mjs and cpu_profile.mjs only see the page's main thread and how
 * long each WebGPU *call* takes there. The work behind a call happens on the GPU
 * process -- Chrome parses shader modules and compiles pipelines on its main
 * thread (CrGpuMain), the same thread that produces the page's frames -- and the
 * startup advances one step per animation frame, so that work can stall the load
 * without ever showing up as a slow call. This traces both processes with Dawn's
 * categories from navigation to the first frame and prints:
 *
 *   - the page main thread: busy vs idle, and V8's lazy wasm compilation;
 *   - the gaps between startup steps (requestAnimationFrame callbacks);
 *   - what CrGpuMain was doing: CreateComputePipeline, CreateShaderModule, ...
 *
 *   node gpu_process_trace.mjs --dir <export-dir> [--args --verbose,...] [--warm] [--after <ms>] [--output trace.json]
 *
 * --after keeps tracing that long past the first frame (default 500 ms) and then lists every CrGpuMain task of
 * 30 ms or more after it, with the Dawn calls inside, and the threads Dawn's pipeline work ran on: what stalls the
 * frames once the game is running, as opposed to during the load.
 *
 * --warm loads the page once (untraced, 25 s) and traces a second load in the same
 * profile, for repeat-visit questions (HTTP and V8 code caches). CI=1 uses the
 * headless software adapter, as the other tools here do; its absolute timings are
 * not real-GPU timings (swiftshader compiles pipelines on the CPU), but which
 * thread waits on which is the same. CHROMIUM_PATH overrides the browser binary.
 */

import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync, mkdtempSync } from 'fs';
import { join, extname, dirname, resolve as resolvePath } from 'path';
import { fileURLToPath } from 'url';
import { tmpdir } from 'os';

const __dirname = dirname(fileURLToPath(import.meta.url));
const arg = (name, fallback) => {
	const i = process.argv.indexOf(`--${name}`);
	return i !== -1 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
};
const DIR = resolvePath(arg('dir', '.'));
const OUTPUT = arg('output', '');
const WARM = process.argv.includes('--warm');
const AFTER_MS = Number(arg('after', '500'));
const EXTRA_ARGS = (arg('args', '') || '').split(',').filter(Boolean);
const SOFTWARE_ADAPTER = !!process.env.CI;

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const server = createServer((req, res) => {
	const p = decodeURIComponent(req.url.split('?')[0]);
	const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) {
		res.writeHead(404);
		res.end();
		return;
	}
	let body = readFileSync(f);
	if (f.endsWith('index.html') && EXTRA_ARGS.length) {
		body = Buffer.from(body.toString().replace(/"args":\s*\[/, `"args":[${EXTRA_ARGS.map((a) => JSON.stringify(a)).join(',')},`));
	}
	res.writeHead(200, {
		'Content-Type': MIME[extname(f)] || 'application/octet-stream',
		'Cross-Origin-Opener-Policy': 'same-origin',
		'Cross-Origin-Embedder-Policy': 'require-corp',
		'Cache-Control': 'public, max-age=3600', // So a --warm second load can use the HTTP cache.
	});
	res.end(body);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

const pwCandidates = [
	join(__dirname, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'),
	...(process.env.PLAYWRIGHT_IMPORT ? [process.env.PLAYWRIGHT_IMPORT] : []),
	'playwright',
];
let pw = null;
for (const candidate of pwCandidates) {
	try {
		pw = await import(candidate);
		break;
	} catch {
		// Try the next one.
	}
}
if (!pw) {
	console.error('Could not load Playwright; npm install in webgpu_tests/scene_smoketest or set PLAYWRIGHT_IMPORT.');
	process.exit(1);
}

const browserArgs = SOFTWARE_ADAPTER
	? ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer', '--use-angle=swiftshader', '--enable-gpu']
	: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist', '--gpu-no-context-lost'];
const executablePath = process.env.CHROMIUM_PATH || (SOFTWARE_ADAPTER ? undefined : '/usr/bin/google-chrome-stable');
const context = await pw.chromium.launchPersistentContext(mkdtempSync(join(tmpdir(), 'godot-gpu-trace-')), {
	headless: SOFTWARE_ADAPTER,
	...(executablePath ? { executablePath } : {}),
	...(SOFTWARE_ADAPTER ? {} : { ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] }),
	args: browserArgs,
	viewport: { width: 1280, height: 720 },
});
const page = await context.newPage();
await page.addInitScript({ path: join(__dirname, 'instrument.js') });
const firstFrame = () => page.waitForFunction(() => window.__godotPhases && window.__godotPhases.marks['godot-webgpu-first-frame'], null, { timeout: 180000 });

if (WARM) {
	await page.goto(url);
	await firstFrame();
	await page.waitForTimeout(25000); // Long enough for V8 to tier up and write its code cache.
	await page.goto('about:blank');
	await page.waitForTimeout(2000);
}

const cdp = await context.newCDPSession(page);
const events = [];
cdp.on('Tracing.dataCollected', (d) => events.push(...d.value));
const complete = new Promise((r) => cdp.once('Tracing.tracingComplete', r));
await cdp.send('Tracing.start', {
	transferMode: 'ReportEvents',
	traceConfig: {
		recordMode: 'recordContinuously',
		includedCategories: ['toplevel', 'v8', 'v8.wasm', 'devtools.timeline', 'disabled-by-default-devtools.timeline',
			'blink.user_timing', 'gpu', 'gpu.dawn', 'disabled-by-default-gpu.dawn'],
	},
});
await page.goto(url);
await firstFrame();
await page.waitForTimeout(AFTER_MS);
const marks = await page.evaluate(() => window.__godotPhases.marks);
await cdp.send('Tracing.end');
await complete;
await context.close();
server.close();
if (OUTPUT) {
	writeFileSync(OUTPUT, JSON.stringify({ marks, events }));
}

// --- Analysis -------------------------------------------------------------

const threadNames = new Map();
for (const e of events) {
	if (e.ph === 'M' && e.name === 'thread_name') {
		threadNames.set(`${e.pid}:${e.tid}`, e.args.name);
	}
}
const nav = events.find((e) => e.name === 'navigationStart');
if (!nav) {
	console.error('No navigationStart in the trace; cannot line the page marks up with it.');
	process.exit(1);
}
const t0 = nav.ts;
const from = t0 + marks['godot-before-callmain'] * 1000;
const to = t0 + marks['godot-webgpu-first-frame'] * 1000;
const threadKey = (name, pid) => [...threadNames].find(([k, v]) => v === name && (pid === undefined || k.startsWith(`${pid}:`)))?.[0];
const mainThread = threadKey('CrRendererMain', nav.pid);
const gpuThread = threadKey('CrGpuMain');

// Top-level busy time on a thread inside the window, plus per-name totals at any depth.
function threadStats(key) {
	const xs = events.filter((e) => `${e.pid}:${e.tid}` === key && e.ph === 'X' && e.ts < to && e.ts + (e.dur || 0) > from)
		.sort((a, b) => a.ts - b.ts || (b.dur || 0) - (a.dur || 0));
	let busy = 0;
	let openUntil = 0;
	const byName = new Map();
	for (const e of xs) {
		const s = Math.max(e.ts, from);
		const end = Math.min(e.ts + (e.dur || 0), to);
		if (e.ts >= openUntil) {
			busy += end - s;
			openUntil = e.ts + (e.dur || 0);
		}
		const t = byName.get(e.name) || { ms: 0, n: 0 };
		t.ms += (end - s) / 1000;
		t.n++;
		byName.set(e.name, t);
	}
	return { busyMs: busy / 1000, byName };
}

const windowMs = (to - from) / 1000;
const main = threadStats(mainThread);
console.log(`callMain -> first frame: ${windowMs.toFixed(0)} ms`);
console.log(`page main thread: busy ${main.busyMs.toFixed(0)} ms, idle ${(windowMs - main.busyMs).toFixed(0)} ms`);
const lazy = main.byName.get('wasm.CompileLazy');
console.log(`  of which V8 lazy wasm compilation: ${lazy ? lazy.ms.toFixed(0) : 0} ms (${lazy ? lazy.n : 0} functions)`);

const steps = events.filter((e) => `${e.pid}:${e.tid}` === mainThread && e.name === 'FireAnimationFrame' && e.dur > 1000 && e.ts > from - 1e6 && e.ts < to)
	.sort((a, b) => a.ts - b.ts);
console.log('startup steps (rAF callbacks over 1 ms) and the idle gap before each:');
let prevent = from;
for (const e of steps) {
	console.log(`  +${((e.ts - from) / 1000).toFixed(0).padStart(6)} ms  ran ${(e.dur / 1000).toFixed(0).padStart(5)} ms  after a ${(Math.max(0, e.ts - prevent) / 1000).toFixed(0).padStart(5)} ms gap`);
	prevent = e.ts + e.dur;
}

if (gpuThread) {
	const gpu = threadStats(gpuThread);
	console.log(`GPU process main thread (CrGpuMain): busy ${gpu.busyMs.toFixed(0)} ms`);
	const dawn = [...gpu.byName].filter(([name]) => name.startsWith('DeviceBase::API') || name.startsWith('Queue::'))
		.sort((a, b) => b[1].ms - a[1].ms).slice(0, 8);
	for (const [name, t] of dawn) {
		console.log(`  ${t.ms.toFixed(0).padStart(7)} ms  x${String(t.n).padStart(4)}  ${name}`);
	}
} else {
	console.log('No CrGpuMain thread in the trace (is the GPU process running in-process?).');
}

if (AFTER_MS > 500 && gpuThread) {
	// After the first frame: every long CrGpuMain task, and the Dawn calls inside it.
	const longTasks = events.filter((e) => `${e.pid}:${e.tid}` === gpuThread && e.ph === 'X' && e.name === 'RunTask' && e.ts >= to && e.dur >= 30000)
		.sort((a, b) => a.ts - b.ts);
	console.log(`\nafter the first frame (${AFTER_MS} ms): CrGpuMain tasks of 30 ms or more: ${longTasks.length}`);
	for (const t of longTasks) {
		const inner = new Map();
		for (const e of events) {
			if (e.pid === t.pid && e.tid === t.tid && e.ph === 'X' && e !== t && e.ts >= t.ts && e.ts < t.ts + t.dur && (e.name.startsWith('DeviceBase::') || /Pipeline|ShaderModule|Tint|Compile/.test(e.name))) {
				const c = inner.get(e.name) || { ms: 0, n: 0 };
				c.ms += e.dur / 1000;
				c.n++;
				inner.set(e.name, c);
			}
		}
		const top = [...inner].sort((a, b) => b[1].ms - a[1].ms).slice(0, 5).map(([n, c]) => `${n} ${c.ms.toFixed(0)} ms x${c.n}`).join(', ');
		console.log(`  +${((t.ts - to) / 1000).toFixed(0).padStart(6)} ms  ${(t.dur / 1000).toFixed(0).padStart(5)} ms  ${top}`);
	}
	// Which threads did pipeline compilation (any process): an async creation that stays off CrGpuMain shows here.
	const byThread = new Map();
	for (const e of events) {
		if (e.ph === 'X' && e.ts >= to && /Pipeline/.test(e.name)) {
			const key = `${threadNames.get(`${e.pid}:${e.tid}`) || `${e.pid}:${e.tid}`} :: ${e.name}`;
			const c = byThread.get(key) || { ms: 0, n: 0 };
			c.ms += e.dur / 1000;
			c.n++;
			byThread.set(key, c);
		}
	}
	console.log('pipeline work after the first frame, by thread:');
	for (const [k, c] of [...byThread].sort((a, b) => b[1].ms - a[1].ms).slice(0, 15)) {
		console.log(`  ${c.ms.toFixed(0).padStart(7)} ms  x${String(c.n).padStart(4)}  ${k}`);
	}
}
