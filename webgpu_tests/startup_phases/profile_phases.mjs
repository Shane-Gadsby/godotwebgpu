/**
 * WebGPU export startup phase profiler -- Task 14 subtask 1.1/1.2/1.3.
 *
 * Brackets every post-download startup phase of an already-exported project and
 * reports how much of the wall clock each one owns, so subtask 2 can be aimed at
 * a measured culprit rather than a guessed one. Needs no engine rebuild: all
 * instrumentation is injected into the page (see instrument.js).
 *
 * Usage:
 *   node profile_phases.mjs --dir <export-dir> [--duration 45] [--headed]
 *                           [--warm] [--label name] [--output path.json]
 *
 * --warm reuses the browser profile between runs so the second run measures a
 * warm HTTP + GPU shader cache; the default is a cold run with a fresh profile,
 * the GPU shader disk cache disabled, and empty origin storage (which is what
 * the user actually experiences on a first visit).
 */

import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync, mkdtempSync } from 'fs';
import { join, extname, dirname, resolve as resolvePath } from 'path';
import { fileURLToPath } from 'url';
import { tmpdir } from 'os';

const __dirname = dirname(fileURLToPath(import.meta.url));

function arg(name, fallback) {
	const i = process.argv.indexOf(`--${name}`);
	return i !== -1 && process.argv[i + 1] && !process.argv[i + 1].startsWith('--') ? process.argv[i + 1] : fallback;
}
const flag = (name) => process.argv.includes(`--${name}`);

const EXPORT_DIR = resolvePath(arg('dir', join(__dirname, '..', 'scene_smoketest', 'exports', 'demo_3d_platformer')));
const DURATION_SEC = parseInt(arg('duration', '45'), 10);
const LABEL = arg('label', 'run');
const OUTPUT_PATH = arg('output', join(__dirname, `phases_${LABEL}.json`));
const WARM = flag('warm');
// --no-console detaches the CDP console listener. Attaching one makes every
// console.log() in the page materially more expensive, so a verbose export
// profiled with it attached overstates the cost of logging; running both ways
// separates the page's own cost from the profiler's observer effect.
const NO_CONSOLE = flag('no-console');
// --args injects extra engine command-line arguments by rewriting GODOT_CONFIG's
// "args" in the served index.html, so a single export can be profiled with and
// without e.g. --verbose without re-exporting it.
// Read directly rather than through arg(), whose ---prefix guard would reject a
// value like "--verbose" -- which is the main thing anyone passes here.
const EXTRA_ARGS = (() => {
	const i = process.argv.indexOf('--args');
	return i !== -1 && process.argv[i + 1] ? process.argv[i + 1] : '';
})();
const HEADED = flag('headed') || !flag('headless');

const MIME_TYPES = {
	'.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
	'.wasm': 'application/wasm', '.pck': 'application/octet-stream',
	'.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml',
	'.ico': 'image/x-icon', '.json': 'application/json', '.wav': 'audio/wav',
	'.ogg': 'audio/ogg', '.mp3': 'audio/mpeg', '.css': 'text/css',
};

function startServer(root) {
	const server = createServer((req, res) => {
		const path = decodeURIComponent(req.url.split('?')[0]);
		const file = join(root, path === '/' ? 'index.html' : path);
		if (!existsSync(file) || !statSync(file).isFile()) {
			res.writeHead(404); res.end('not found'); return;
		}
		if (EXTRA_ARGS && file.endsWith('index.html')) {
			const injected = EXTRA_ARGS.split(',').map((a) => JSON.stringify(a.trim())).join(',');
			const html = readFileSync(file, 'utf8').replace('"args":[]', `"args":[${injected}]`);
			res.writeHead(200, { 'Content-Type': 'text/html', 'Cache-Control': 'no-store' });
			res.end(html);
			return;
		}
		// Cross-origin isolation headers, so a threads=yes export would also load
		// here -- harmless for the nothreads build this normally profiles.
		res.writeHead(200, {
			'Content-Type': MIME_TYPES[extname(file).toLowerCase()] || 'application/octet-stream',
			'Content-Length': statSync(file).size,
			'Cross-Origin-Opener-Policy': 'same-origin',
			'Cross-Origin-Embedder-Policy': 'require-corp',
			'Cache-Control': 'no-store',
		});
		res.end(readFileSync(file));
	});
	return new Promise((res) => server.listen(0, '127.0.0.1', () => res(server)));
}

const sum = (xs) => xs.reduce((a, b) => a + b, 0);
const fmt = (ms) => (ms === null || ms === undefined || Number.isNaN(ms) ? '     --  ' : `${ms.toFixed(0).padStart(7)}ms`);
const mb = (bytes) => `${(bytes / 1048576).toFixed(1)}MB`;

async function main() {
	if (!existsSync(join(EXPORT_DIR, 'index.html'))) {
		console.error(`No index.html in ${EXPORT_DIR} -- pass --dir <export-dir>.`);
		process.exit(1);
	}
	const server = await startServer(EXPORT_DIR);
	const url = `http://127.0.0.1:${server.address().port}/index.html`;

	const { chromium } = await import(join(__dirname, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
	const args = ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--gpu-no-context-lost'];
	if (!WARM) { args.push('--disable-gpu-shader-disk-cache', '--disable-gpu-program-cache'); }

	const userDataDir = WARM
		? join(tmpdir(), 'godot-startup-phases-warm-profile')
		: mkdtempSync(join(tmpdir(), 'godot-startup-phases-'));

	const context = await chromium.launchPersistentContext(userDataDir, {
		headless: !HEADED,
		args,
		viewport: { width: 1280, height: 720 },
	});

	const consoleLog = [];
	const wallStart = Date.now();
	const page = await context.newPage();
	if (!NO_CONSOLE) { page.on('console', (msg) => consoleLog.push({ atMs: Date.now() - wallStart, type: msg.type(), text: msg.text().substring(0, 400) }));
	}
	page.on('pageerror', (err) => consoleLog.push({ atMs: Date.now() - wallStart, type: 'pageerror', text: String(err.message).substring(0, 400) }));

	if (flag('eager-pipelines')) {
		await page.addInitScript('window.__eagerPipelines = true;');
	}
	const flushEveryMB = arg('flush-every-mb', '');
	if (flushEveryMB) {
		await page.addInitScript(`window.__flushEveryMB = ${parseFloat(flushEveryMB)};`);
	}
	await page.addInitScript({ path: join(__dirname, 'instrument.js') });

	console.log(`Export:   ${EXPORT_DIR}`);
	console.log(`Args:     ${EXTRA_ARGS || '(none)'}   console listener: ${NO_CONSOLE ? 'DETACHED' : 'attached'}`);
	console.log(`Cache:    ${WARM ? 'WARM (reused browser profile)' : 'COLD (fresh profile, GPU shader cache disabled)'}`);
	console.log(`Loading   ${url} -- observing for ${DURATION_SEC}s ...\n`);

	await page.goto(url, { waitUntil: 'domcontentloaded' });
	await page.waitForTimeout(DURATION_SEC * 1000);

	const data = await page.evaluate(() => {
		const P = window.__godotPhases;
		if (!P) { return null; }
		return {
			marks: P.marks, events: P.events, wasm: P.wasm, fetches: P.fetches,
			shaderModules: P.shaderModules, renderPipelines: P.renderPipelines,
			computePipelines: P.computePipelines, gpuWrites: P.gpuWrites, gpuCreates: P.gpuCreates,
			frames: P.frames,
			eager: { kicked: P.eagerKicked, done: P.eagerDone, failed: P.eagerFailed },
			probeFlushes: P.probeFlushes,
			shaderStats: window.godotWebGPUShaderStats || null,
			// Phase marks pushed from C++ inside callMain() -- present only when the
			// export is run with --benchmark (see OS_Web::benchmark_end_measure).
			engineMarks: window.godotStartupMarks || null,
			nav: performance.getEntriesByType('navigation').map((e) => ({ name: e.name, responseEnd: e.responseEnd, domContentLoaded: e.domContentLoadedEventEnd })),
			resources: performance.getEntriesByType('resource')
				.filter((e) => /\.(wasm|pck)$/.test(e.name))
				.map((e) => ({ name: e.name.split('/').pop(), startMs: e.startTime, responseStartMs: e.responseStart, endMs: e.responseEnd, bytes: e.encodedBodySize || e.transferSize })),
		};
	});

	await context.close();
	server.close();

	if (!data) { console.error('No instrumentation data -- did the page fail to load?'); process.exit(1); }
	data.meta = { exportDir: EXPORT_DIR, label: LABEL, warm: WARM, durationSec: DURATION_SEC, when: new Date().toISOString() };
	data.consoleLog = consoleLog;
	writeFileSync(OUTPUT_PATH, JSON.stringify(data, null, 2));
	report(data);
	console.log(`\nFull data: ${OUTPUT_PATH}`);
}

function report(d) {
	const m = d.marks;
	const line = (label, value, note) => console.log(`  ${fmt(value)}  ${label.padEnd(34)}${note || ''}`);
	const bar = (c) => console.log(c.repeat(78));

	bar('=');
	console.log(`STARTUP PHASES -- ${d.meta.label} (${d.meta.warm ? 'warm' : 'cold'})`);
	bar('=');

	// --- Downloads -----------------------------------------------------------
	console.log('\n--- Downloads (Resource Timing) ---');
	for (const r of d.resources) {
		line(r.name, r.endMs - r.startMs, `${mb(r.bytes || 0)}  (first byte at ${(r.responseStartMs || 0).toFixed(0)}ms)`);
	}

	// --- WASM compile/instantiate -------------------------------------------
	console.log('\n--- WASM compile / instantiate ---');
	if (!d.wasm.length) {
		console.log('  (no WebAssembly.* call observed -- instrumentation may have been installed too late)');
	}
	for (const w of d.wasm) {
		const streaming = w.api.endsWith('Streaming');
		line(w.api, w.durationMs, `${streaming ? 'STREAMING' : 'NON-STREAMING (fallback!)'}  mime=${w.mime || 'n/a'}  ${w.bytes ? mb(w.bytes) : ''}`);
	}

	// --- Device acquisition --------------------------------------------------
	console.log('\n--- WebGPU device acquisition ---');
	line('requestAdapter', span(m, 'requestAdapter_start', 'requestAdapter_done'), `starts at ${fmt(m.requestAdapter_start).trim()}`);
	line('requestDevice', span(m, 'requestDevice_start', 'requestDevice_done'), `starts at ${fmt(m.requestDevice_start).trim()}`);

	// --- The callMain black box ---------------------------------------------
	const cmStart = m['godot-before-callmain'];
	const cmEnd = m['godot-webgpu-first-frame'];
	const cmWindow = span(m, 'godot-before-callmain', 'godot-webgpu-first-frame');
	console.log('\n--- callMain() window: before-callmain -> first presented frame ---');
	console.log('    (this is the post-"100%" stall; JS cannot paint during it)');
	line('total stall', cmWindow, cmStart !== undefined ? `from ${fmt(cmStart).trim()} to ${fmt(cmEnd).trim()}` : 'NOT OBSERVED');

	const inWindow = (xs) => (cmStart === undefined ? [] : xs.filter((x) => x.startMs >= cmStart - 1 && (cmEnd === undefined || x.startMs <= cmEnd)));
	const accounted = [
		['createShaderModule', inWindow(d.shaderModules)],
		['createRenderPipeline', inWindow(d.renderPipelines)],
		['createComputePipeline', inWindow(d.computePipelines)],
		['queue writes / submit', inWindow(d.gpuWrites)],
		['createTexture / createBuffer', inWindow(d.gpuCreates)],
	];
	let accountedTotal = 0;
	for (const [name, xs] of accounted) {
		const t = sum(xs.map((x) => x.durationMs));
		accountedTotal += t;
		const bytes = sum(xs.map((x) => x.bytes || 0));
		line(name, t, `${String(xs.length).padStart(5)} calls${bytes ? `, ${mb(bytes)}` : ''}`);
	}
	line('WebGPU API total', accountedTotal, cmWindow ? `${((accountedTotal / cmWindow) * 100).toFixed(1)}% of the stall` : '');
	const unaccounted = cmWindow === null ? null : cmWindow - accountedTotal;
	line('NOT in a WebGPU call', unaccounted,
		cmWindow ? `${((unaccounted / cmWindow) * 100).toFixed(1)}% -- CPU: resource decode, scene parse, GDScript, engine init` : '');

	// --- Engine-reported phases from inside callMain -------------------------
	if (d.engineMarks && d.engineMarks.length) {
		console.log('\n--- Engine phases reported from inside callMain() (--benchmark) ---');
		console.log('    (nested phases overlap their parent; "Startup:*" are the top level)');
		for (const k of d.engineMarks.slice().sort((a, b) => a.startMs - b.startMs)) {
			line(k.name, k.durationMs, `${k.startMs.toFixed(0)} -> ${k.endMs.toFixed(0)} ms${cmStart !== undefined ? `  (callMain+${(k.startMs - cmStart).toFixed(0)})` : ''}`);
		}
		// What the top-level phases do not account for: time inside callMain that no
		// Startup:* bracket covers, which is where a gap in main.cpp's own
		// instrumentation would hide.
		const top = d.engineMarks.filter((k) => k.name.startsWith('Startup:') && k.name !== 'Startup:Main::Setup2' && k.name !== 'Startup:Main::Setup');
		const topTotal = sum(d.engineMarks.filter((k) => ['Startup:Main::Setup', 'Startup:Main::Setup2', 'Startup:Main::Start'].includes(k.name)).map((k) => k.durationMs));
		line('Setup + Setup2 + Start', topTotal, cmWindow ? `${((topTotal / cmWindow) * 100).toFixed(1)}% of the stall; the rest is after Main::start() returns` : '');
		void top;
	}

	// --- Independent confirmation from the rAF gap ---------------------------
	console.log('\n--- Longest main-thread block (rAF gap, independent of engine events) ---');
	let worst = 0; let worstAt = 0;
	for (let i = 1; i < d.frames.length; i++) {
		const gap = d.frames[i] - d.frames[i - 1];
		if (gap > worst) { worst = gap; worstAt = d.frames[i - 1]; }
	}
	line('longest rAF gap', worst, `starting at ${fmt(worstAt).trim()}`);

	// --- Shader translation sanity check ------------------------------------
	console.log('\n--- godotWebGPUShaderStats (Task 34: translated must be 0) ---');
	console.log(`  ${d.shaderStats ? JSON.stringify(d.shaderStats) : '(not exposed -- engine may not have reached that point)'}`);

	// --- Whole timeline ------------------------------------------------------
	console.log('\n--- All marks, in order ---');
	for (const [name, ms] of Object.entries(m).sort((a, b) => a[1] - b[1])) {
		line(name, ms);
	}
	bar('=');
}

function span(marks, a, b) {
	if (marks[a] === undefined || marks[b] === undefined) { return null; }
	return marks[b] - marks[a];
}

main().catch((e) => { console.error(e); process.exit(1); });
