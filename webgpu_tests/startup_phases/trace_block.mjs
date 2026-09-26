/**
 * Chrome GPU trace of the startup upload cliff (Task 14 subtask 2).
 *
 * Loads an export with tracing on, finds the one blocking queue call the page
 * instrumentation reports, and prints the trace slices that overlap it -- which is
 * what says whether the main thread is waiting on the GPU process, on IPC, or on
 * something in the renderer itself.
 *
 *   node trace_block.mjs --dir <export-dir> [--duration 20]
 */
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync, mkdtempSync } from 'fs';
import { join, extname, dirname, resolve } from 'path';
import { fileURLToPath } from 'url';
import { tmpdir } from 'os';

const __dirname = dirname(fileURLToPath(import.meta.url));
const argOf = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 && process.argv[i + 1] ? process.argv[i + 1] : d; };
const DIR = resolve(argOf('dir', '.'));
const DURATION = parseInt(argOf('duration', '20'), 10);
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };

const server = createServer((req, res) => {
	const p = decodeURIComponent(req.url.split('?')[0]);
	const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
	res.writeHead(200, { 'Content-Type': MIME[extname(f).toLowerCase()] || 'application/octet-stream', 'Content-Length': statSync(f).size, 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
	res.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));

const { chromium } = await import(join(__dirname, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
const ctx = await chromium.launchPersistentContext(mkdtempSync(join(tmpdir(), 'trace-')), {
	headless: false,
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--gpu-no-context-lost', '--disable-gpu-shader-disk-cache', '--disable-gpu-program-cache'],
	viewport: { width: 1280, height: 720 },
});
const page = await ctx.newPage();
await page.addInitScript({ path: join(__dirname, 'instrument.js') });

const client = await ctx.newCDPSession(page);
const events = [];
client.on('Tracing.dataCollected', (d) => { events.push(...d.value); });
const complete = new Promise((r) => client.once('Tracing.tracingComplete', r));

await client.send('Tracing.start', {
	transferMode: 'ReportEvents',
	traceConfig: {
		recordMode: 'recordContinuously',
		includedCategories: ['toplevel', 'gpu', 'viz', 'cc', 'blink', 'ipc', 'mojom', 'sequence_manager',
			'disabled-by-default-gpu.device', 'disabled-by-default-gpu.service', 'disabled-by-default-toplevel.flow'],
	},
});

await page.goto(`http://127.0.0.1:${server.address().port}/index.html`, { waitUntil: 'domcontentloaded' });
await page.waitForTimeout(DURATION * 1000);

const phases = await page.evaluate(() => {
	const P = window.__godotPhases;
	const slow = P.gpuWrites.slice().sort((a, b) => b.durationMs - a.durationMs)[0];
	return { timeOrigin: performance.timeOrigin, t0: P.t0, slow: slow, marks: P.marks };
});

await client.send('Tracing.end');
await complete;
await ctx.close();
server.close();

console.log(`trace events: ${events.length}`);
console.log(`blocking call: ${phases.slow.label} ${phases.slow.durationMs.toFixed(0)}ms at page-relative ${phases.slow.startMs.toFixed(0)}ms`);

// Trace timestamps are microseconds on the system monotonic clock; performance.now()
// is milliseconds since timeOrigin on the same base, so this maps the block into it.

// The trace clock (CLOCK_MONOTONIC microseconds) and performance.now() do not share
// a base, so rather than mapping between them, find the block inside the trace: it is
// by far the longest slice on the renderer's main thread, and its duration is already
// known from the page instrumentation.
const longest = events.filter((e) => e.ts && e.dur).sort((a, b) => b.dur - a.dur).slice(0, 30);
console.log('\nlongest slices anywhere in the trace:');
for (const e of longest) {
	console.log(`  ${(e.dur / 1000).toFixed(1).padStart(8)}ms  pid=${e.pid} tid=${e.tid}  ${e.cat} | ${e.name}`.substring(0, 140));
}
// Take the slice whose duration best matches the measured block, then report what ran
// in every process while it was open.
const target = events.filter((e) => e.ts && e.dur)
	.map((e) => ({ e, diff: Math.abs(e.dur / 1000 - phases.slow.durationMs) }))
	.sort((a, b) => a.diff - b.diff)[0];
console.log(`\nbest match for the ${phases.slow.durationMs.toFixed(0)}ms block: ${(target.e.dur / 1000).toFixed(1)}ms  pid=${target.e.pid} tid=${target.e.tid}  ${target.e.cat} | ${target.e.name}`);
const blockStartUs2 = target.e.ts;
const blockEndUs2 = target.e.ts + target.e.dur;
const overlapping = events.filter((e) => e.ts && e.dur && e.ts < blockEndUs2 && (e.ts + e.dur) > blockStartUs2 && e !== target.e);
console.log(`slices overlapping the block: ${overlapping.length}`);

const byName = {};
for (const e of overlapping) {
	const key = `${e.cat} | ${e.name}`;
	byName[key] = byName[key] || { n: 0, dur: 0, pid: e.pid, tid: e.tid };
	byName[key].n++;
	byName[key].dur += e.dur;
}
console.log('\ntop overlapping slices by total duration inside the block:');
for (const [k, v] of Object.entries(byName).sort((a, b) => b[1].dur - a[1].dur).slice(0, 25)) {
	console.log(`  ${(v.dur / 1000).toFixed(1).padStart(8)}ms  ${String(v.n).padStart(5)}x  pid=${v.pid} tid=${v.tid}  ${k.substring(0, 110)}`);
}
writeFileSync(join(__dirname, 'trace_block.json'), JSON.stringify({ block: phases.slow, overlapping: overlapping.slice(0, 5000) }, null, 1));
console.log('\nwrote trace_block.json');
