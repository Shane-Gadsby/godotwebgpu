// Occlusion-culling benchmark on the web (godot/occ.gd says what the scene does).
//
//   node run.mjs --dir <export dir> [--query 'n=6000&rounds=3'] [--json out.json] [--uncapped]
//
// Serves the export (COOP/COEP, so threaded exports work), opens it in system Chrome on the real GPU (headed, TASKS.md
// §3 trap 43), and reports per phase (street/top view x occlusion culling off/on, mean over the rounds):
//   raf_ms   main-thread time inside each requestAnimationFrame callback -- in a nothreads export the whole engine
//            frame (script, culling, draw submission). A threaded export with a separate render thread renders on a
//            worker, which this does not see.
//   gpu_ms   GPU time per frame: timestamps around every command encoder made on the main thread (nothreads only).
//   draws/objects, vp_cpu_ms  from the scene (RenderingServer rendering info, viewport measured CPU render time).
// Also: time from navigation to the scene's first _ready (load), and the wasm heap size at ready and at the end.
// --uncapped lifts Chrome's frame-rate limit and paces frames to 2 in flight (TASKS.md §3 trap 66).
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const QUERY = arg('query', '');
const JSON_OUT = arg('json', '');
const UNCAPPED = process.argv.includes('--uncapped');
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Content-Length': statSync(f).size, 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const { chromium } = await import(join(HERE, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
const browser = await chromium.launch({
	headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist', '--enable-webgpu-developer-features',
		...(UNCAPPED ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : [])],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1 });
await page.addInitScript((pace) => {
	const grab = (res) => {
		const ex = (res && (res.instance || res).exports) || {};
		for (const k in ex) if (ex[k] instanceof Mem) window.__mem = ex[k];
		return res;
	};
	// A threaded export creates its (shared) memory in JS and imports it; a nothreads one exports it.
	const Mem = WebAssembly.Memory;
	WebAssembly.Memory = function (d) { const m = new Mem(d); window.__mem = window.__mem || m; return m; };
	WebAssembly.Memory.prototype = Mem.prototype;
	const inst = WebAssembly.instantiate;
	WebAssembly.instantiate = (...a) => inst.apply(WebAssembly, a).then(grab);
	const st = WebAssembly.instantiateStreaming;
	if (st) WebAssembly.instantiateStreaming = (...a) => st.apply(WebAssembly, a).then(grab);
	const heap = () => (window.__mem ? window.__mem.buffer.byteLength : 0);
	window.__occ = { phases: {}, marks: {} };
	let cur = null;
	window.__occMark = (what) => {
		const now = performance.now();
		window.__occ.marks[what] = { t: now, heap: heap() };
		if (what.startsWith('start:')) cur = { sum: 0, n: 0, max: 0, gpu: 0, gpuFrames: 0 };
		else if (what.startsWith('end:') && cur) { (window.__occ.phases[what.slice(4)] ||= []).push(cur); cur = null; }
	};
	let inflight = [], last = null;
	if (pace && typeof GPUQueue !== 'undefined') {
		const submit = GPUQueue.prototype.submit;
		GPUQueue.prototype.submit = function (...a) { const r = submit.apply(this, a); const p = this.onSubmittedWorkDone(); p.then(() => { p.ok = true; }, () => { p.ok = true; }); last = p; return r; };
	}
	// GPU time: an empty compute pass with a timestamp write opens and closes every command encoder the engine makes on
	// this thread (as perf_guard/gpu_time.mjs does); once per frame the spans are resolved, read back and summed into
	// the phase that submitted them. Main thread only, so a threaded export rendering on a worker reads 0.
	const G = { dev: null, qs: null, n: 0, cap: 512, resolve: null, free: [] };
	if (typeof GPUAdapter !== 'undefined') {
		const rd = GPUAdapter.prototype.requestDevice;
		GPUAdapter.prototype.requestDevice = async function (desc = {}) {
			const d = { ...desc };
			if (this.features.has('timestamp-query')) d.requiredFeatures = [...(desc.requiredFeatures || []), 'timestamp-query'];
			const dev = await rd.call(this, d);
			if (dev.features.has('timestamp-query') && !G.dev) {
				G.dev = dev;
				G.qs = dev.createQuerySet({ type: 'timestamp', count: G.cap * 2 });
				G.resolve = dev.createBuffer({ size: G.cap * 16, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
				for (let i = 0; i < 6; i++) G.free.push(dev.createBuffer({ size: G.cap * 16, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST }));
			}
			return dev;
		};
		const ce = GPUDevice.prototype.createCommandEncoder;
		G.ce = ce;
		GPUDevice.prototype.createCommandEncoder = function (...a) {
			const e = ce.apply(this, a);
			if (this === G.dev && cur && G.n < G.cap) {
				const k = G.n++;
				e.__ts = k;
				e.beginComputePass({ timestampWrites: { querySet: G.qs, beginningOfPassWriteIndex: 2 * k } }).end();
			}
			return e;
		};
		const fin = GPUCommandEncoder.prototype.finish;
		GPUCommandEncoder.prototype.finish = function (...a) {
			if (this.__ts !== undefined) this.beginComputePass({ timestampWrites: { querySet: G.qs, endOfPassWriteIndex: 2 * this.__ts + 1 } }).end();
			return fin.apply(this, a);
		};
	}
	const gpuFlush = () => {
		if (!G.n) return;
		const n = G.n, ph = cur; G.n = 0;
		if (!ph || !G.free.length) return;
		const rb = G.free.pop();
		const e = G.ce.call(G.dev);
		e.resolveQuerySet(G.qs, 0, n * 2, G.resolve, 0);
		e.copyBufferToBuffer(G.resolve, 0, rb, 0, n * 16);
		G.dev.queue.submit([e.finish()]);
		rb.mapAsync(GPUMapMode.READ, 0, n * 16).then(() => {
			const v = new BigUint64Array(rb.getMappedRange(0, n * 16));
			let ns = 0;
			for (let i = 0; i < n; i++) { const b = v[2 * i], en = v[2 * i + 1]; if (b && en > b) ns += Number(en - b); }
			rb.unmap(); G.free.push(rb);
			ph.gpu += ns / 1e6; ph.gpuFrames++;
		}, () => G.free.push(rb));
	};
	const raf = window.requestAnimationFrame.bind(window);
	window.requestAnimationFrame = (cb) => raf(function tick(ts) {
		const run = () => {
			const t0 = performance.now();
			cb(ts);
			const d = performance.now() - t0;
			if (cur) { cur.sum += d; cur.n++; cur.max = Math.max(cur.max, d); }
			gpuFlush();
		};
		if (pace) {
			if (last) { inflight.push(last); last = null; }
			while (inflight.length > 2) inflight.shift();
			const g = inflight.length === 2 ? inflight[0] : null;
			if (g && !g.ok) { g.then(run, run); return; }
		}
		run();
	});
}, UNCAPPED);
const lines = [];
let done;
const finished = new Promise((r) => { done = r; });
page.on('console', (m) => {
	const t = m.text();
	if (t.startsWith('[OCC]')) { console.log(t); lines.push(t); }
	else if (/error|lost/i.test(t)) console.log('  console: ' + t.slice(0, 200));
	if (t.startsWith('[OCC] done')) done();
});
page.on('pageerror', (e) => console.log('  pageerror: ' + e.message));
await page.goto(`http://127.0.0.1:${server.address().port}/${QUERY ? '?' + QUERY : ''}`);
const timeout = setTimeout(() => { console.log('TIMEOUT'); done(); }, 600000);
await finished;
clearTimeout(timeout);
const occ = await page.evaluate(() => window.__occ);
const adapter = await page.evaluate(async () => { const a = await navigator.gpu.requestAdapter(); return a ? `${a.info.vendor}/${a.info.architecture}/${a.info.description}` : 'none'; });
await browser.close();
server.close();

const res = { dir: DIR, adapter, uncapped: UNCAPPED, load_ms: occ.marks.ready && +occ.marks.ready.t.toFixed(0),
	heap_ready_mb: occ.marks.ready && occ.marks.ready.heap / 1048576, heap_end_mb: occ.marks.done && occ.marks.done.heap / 1048576, phases: {} };
for (const l of lines) {
	const m = l.match(/phase=(\S+) round=(\d+) (.*)/); if (!m) continue;
	const kv = Object.fromEntries(m[3].split(' ').map((x) => x.split('=')).map(([k, v]) => [k, +v]));
	(res.phases[m[1]] ||= { rounds: [] }).rounds.push(kv);
}
const mean = (a) => a.reduce((s, x) => s + x, 0) / a.length;
console.log(`adapter ${adapter}  load ${res.load_ms} ms  heap ready ${res.heap_ready_mb} MB end ${res.heap_end_mb} MB`);
console.log('phase          fps    raf_ms  raf_max  gpu_ms  draws  objects  vp_cpu_ms');
for (const [name, p] of Object.entries(res.phases)) {
	const raf = (occ.phases[name] || []).map((x) => x.sum / Math.max(1, x.n));
	const rmax = (occ.phases[name] || []).map((x) => x.max);
	const gpu = (occ.phases[name] || []).filter((x) => x.gpuFrames).map((x) => x.gpu / x.gpuFrames);
	Object.assign(p, { fps: mean(p.rounds.map((r) => r.fps)), raf_ms: mean(raf), raf_max_ms: Math.max(...rmax), gpu_ms: gpu.length ? mean(gpu) : NaN,
		draws: mean(p.rounds.map((r) => r.draws)), objects: mean(p.rounds.map((r) => r.objects)),
		vp_cpu_ms: mean(p.rounds.map((r) => r.vp_cpu_ms)), vp_gpu_ms: mean(p.rounds.map((r) => r.vp_gpu_ms)) });
	console.log(`${name.padEnd(12)} ${p.fps.toFixed(1).padStart(6)} ${p.raf_ms.toFixed(2).padStart(8)} ${p.raf_max_ms.toFixed(1).padStart(8)} ${p.gpu_ms.toFixed(2).padStart(7)} ${p.draws.toFixed(0).padStart(6)} ${p.objects.toFixed(0).padStart(8)} ${p.vp_cpu_ms.toFixed(2).padStart(10)}`);
}
if (JSON_OUT) writeFileSync(JSON_OUT, JSON.stringify(res, null, 1));
