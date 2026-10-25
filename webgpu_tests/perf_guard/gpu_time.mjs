// How long does a WebGPU export's work take *on the GPU*? (TASKS.md 2.30)
//
//   node gpu_time.mjs --dir <export dir> [--browser chrome|firefox] [--uncapped] [--passes [--top 30] [--dims]] [--rounds N] [--settle 12000] [--size 1280x720]
//
// api_profile.mjs says where the main thread's time goes, but uncapped, a main thread that is "100% busy" can be
// blocked in getCurrentTexture()/submit() waiting for the GPU or the compositor, which reads exactly like CPU cost.
// This settles it: every command encoder the engine creates gets an empty compute pass with a timestamp write at its
// start and another just before finish(), so the GPU-side span of every submitted command buffer is measured. Reports
// GPU ms per frame (sum of spans) against wall ms per frame; GPU ms close to the frame time means GPU-bound.
// --passes times every render/compute pass instead and ranks them by GPU ms/frame, named by pipeline and attachment.
// Needs the adapter's 'timestamp-query' feature; --enable-webgpu-developer-features lifts the 100 us quantization.
// Same launch as api_profile.mjs (system Chrome, standard flags).
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import { FIREFOX_WEBGPU_PREFS } from '../scene_smoketest/firefox_prefs.mjs';
const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const SETTLE = +arg('settle', 12000);
const UNCAPPED = process.argv.includes('--uncapped');
const SIZE = arg('size', '');
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
// COOP/COEP on every response: threaded exports need cross-origin isolation, and same-origin exports are unaffected.
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const { chromium, firefox } = await import(join(HERE, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
// --browser firefox: Firefox exposes 'timestamp-query' too (work task 11); uncapped there is layout.frame_rate=0 (ASAP mode).
const BROWSER = arg('browser', 'chrome');
const browser = BROWSER === 'firefox' ? await firefox.launch({
	headless: false, firefoxUserPrefs: { ...FIREFOX_WEBGPU_PREFS, ...(UNCAPPED ? { 'layout.frame_rate': 0 } : {}) },
}) : await chromium.launch({
	headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist', '--enable-webgpu-developer-features',
		...(UNCAPPED ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : [])],
});
const page = await browser.newPage(SIZE ? { viewport: { width: +SIZE.split('x')[0], height: +SIZE.split('x')[1] } } : {});
page.on('console', (m) => { if (m.text().startsWith('[gpu_time]')) console.log(m.text()); });
const PASSES = process.argv.includes('--passes');
const DIMS = process.argv.includes('--dims'); // --passes: also split rows by dispatchWorkgroups size
// --replace-wgsl <label substring>=<file>: substitute a shader module's WGSL (bisecting a slow shader by hand-editing it).
const REPLACE = process.argv.flatMap((a, i) => (a === '--replace-wgsl' ? [process.argv[i + 1]] : [])).map((x) => { const j = x.indexOf('='); return [x.slice(0, j), readFileSync(x.slice(j + 1), 'utf8')]; });
await page.addInitScript((REPLACE) => {
	if (!REPLACE.length) return;
	const c = GPUDevice.prototype.createShaderModule;
	GPUDevice.prototype.createShaderModule = function (d) {
		const r = d && d.label && REPLACE.find(([sub]) => d.label.includes(sub));
		if (r) console.log(`[gpu_time] replaced WGSL of ${d.label}`);
		return c.call(this, r ? { ...d, code: r[1] } : d);
	};
}, REPLACE);
await page.addInitScript((PASSES) => {
	// 4096 queries: by command buffer that is 2048 encoders (~1000 frames); by pass (--passes) ~18 frames of a 110-pass frame.
	const PAIRS = 2048;
	const G = window.__gt = { on: false, used: 0, frames: 0, t0: 0, t1: 0, qs: null, device: null, enc: new WeakMap(), pass: new WeakMap(), meta: [], copies: 0, views: new WeakMap() };
	const rd = GPUAdapter.prototype.requestDevice;
	GPUAdapter.prototype.requestDevice = async function (desc = {}) {
		const d = { ...desc };
		// Which adapter a number came from: swiftshader runs Forward Mobile (CLAUDE.md), so a timing taken on it is not a Forward+ timing.
		const ai = this.info || {};
		console.log(`[gpu_time] adapter ${ai.vendor || '?'}/${ai.architecture || '?'} ${ai.description || ''}, maxSampledTexturesPerShaderStage ${this.limits.maxSampledTexturesPerShaderStage}`);
		if (this.features.has('timestamp-query')) d.requiredFeatures = [...(desc.requiredFeatures || []), 'timestamp-query'];
		else console.log('[gpu_time] adapter has no timestamp-query; nothing will be measured');
		const dev = await rd.call(this, d);
		if (dev.features.has('timestamp-query')) {
			G.device = dev;
			G.qs = dev.createQuerySet({ type: 'timestamp', count: PAIRS * 2 });
		}
		return dev;
	};
	const full = () => G.used >= PAIRS;
	const stop = () => { if (!G.t1) G.t1 = performance.now(); };
	if (!PASSES) {
		const ce = GPUDevice.prototype.createCommandEncoder;
		GPUDevice.prototype.createCommandEncoder = function (...a) {
			const e = ce.apply(this, a);
			if (G.on && this === G.device && !full()) {
				const k = G.used++;
				G.enc.set(e, k);
				G.meta[k] = { frame: G.frames };
				e.beginComputePass({ timestampWrites: { querySet: G.qs, beginningOfPassWriteIndex: 2 * k } }).end();
			}
			return e;
		};
		const fin = GPUCommandEncoder.prototype.finish;
		GPUCommandEncoder.prototype.finish = function (...a) {
			const k = G.enc.get(this);
			if (k !== undefined) {
				this.beginComputePass({ timestampWrites: { querySet: G.qs, endOfPassWriteIndex: 2 * k + 1 } }).end();
				if (full()) stop();
			}
			return fin.apply(this, a);
		};
	} else {
		// Name a pass by its first attachment's texture label and the pipelines it runs.
		const cv = GPUTexture.prototype.createView;
		GPUTexture.prototype.createView = function (...a) { const v = cv.apply(this, a); G.views.set(v, this.label || `${this.width}x${this.height}x${this.depthOrArrayLayers} ${this.format}`); return v; };
		const hook = (cls, begin) => {
			const b = GPUCommandEncoder.prototype[begin];
			GPUCommandEncoder.prototype[begin] = function (desc = {}) {
				if (!G.on || full() || desc.timestampWrites) return b.call(this, desc);
				const k = G.used++;
				const att = desc.colorAttachments && desc.colorAttachments[0] ? desc.colorAttachments[0].view : (desc.depthStencilAttachment ? desc.depthStencilAttachment.view : null);
				G.meta[k] = { frame: G.frames, type: cls === 'GPURenderPassEncoder' ? 'render' : 'compute', att: att ? (G.views.get(att) || '?') : '', pipes: [], n: 0, dims: [] };
				const p = b.call(this, { ...desc, timestampWrites: { querySet: G.qs, beginningOfPassWriteIndex: 2 * k, endOfPassWriteIndex: 2 * k + 1 } });
				G.pass.set(p, k);
				if (full()) stop();
				return p;
			};
			const sp = window[cls].prototype.setPipeline;
			window[cls].prototype.setPipeline = function (pl) {
				const k = G.pass.get(this);
				if (k !== undefined) { const m = G.meta[k]; const l = (pl.label || '?').replace(/^c?pipe#\d+:/, ''); if (!m.pipes.includes(l)) m.pipes.push(l); }
				return sp.call(this, pl);
			};
			for (const d of cls === 'GPURenderPassEncoder' ? ['draw', 'drawIndexed', 'drawIndirect', 'drawIndexedIndirect'] : ['dispatchWorkgroups', 'dispatchWorkgroupsIndirect']) {
				const f = window[cls].prototype[d];
				window[cls].prototype[d] = function (...a) {
					const k = G.pass.get(this);
					if (k !== undefined) { const m = G.meta[k]; m.n++; if (d === 'dispatchWorkgroups') { const g = `${a[0]}x${a[1] ?? 1}x${a[2] ?? 1}`; if (!m.dims.includes(g)) m.dims.push(g); } }
					return f.apply(this, a);
				};
			}
		};
		hook('GPURenderPassEncoder', 'beginRenderPass');
		hook('GPUComputePassEncoder', 'beginComputePass');
		// Copies run between passes, so bracket each with two empty timestamped compute passes to time it like one.
		const texName = (t) => t.label || `${t.width}x${t.height}x${t.depthOrArrayLayers} ${t.format}`;
		const timeCopy = (name, describe) => {
			const f = GPUCommandEncoder.prototype[name];
			GPUCommandEncoder.prototype[name] = function (...a) {
				if (!G.on || full()) return f.apply(this, a);
				if (name === 'copyTextureToTexture') G.copies++;
				const k = G.used++;
				G.meta[k] = { frame: G.frames, type: 'copy', att: describe(a), pipes: [name], n: 1, dims: [] };
				this.beginComputePass({ timestampWrites: { querySet: G.qs, beginningOfPassWriteIndex: 2 * k } }).end();
				const r = f.apply(this, a);
				this.beginComputePass({ timestampWrites: { querySet: G.qs, endOfPassWriteIndex: 2 * k + 1 } }).end();
				if (full()) stop();
				return r;
			};
		};
		const sz = (s) => (Array.isArray(s) ? s.join('x') : `${s.width}x${s.height ?? 1}x${s.depthOrArrayLayers ?? 1}`);
		timeCopy('copyTextureToTexture', (a) => `${texName(a[0].texture)} region ${sz(a[2])}`);
		timeCopy('copyBufferToTexture', (a) => `${texName(a[1].texture)} region ${sz(a[2])}`);
		timeCopy('copyBufferToBuffer', (a) => `${a[4] ?? '?'} B`);
	}
	const raf = window.requestAnimationFrame.bind(window);
	window.requestAnimationFrame = (cb) => raf((t) => { if (G.on && !full()) G.frames++; cb(t); });
}, PASSES);
await page.goto(`http://127.0.0.1:${server.address().port}/`);
await new Promise((r) => setTimeout(r, SETTLE));
// --rounds N: repeat the measurement window N times and pool the results (one window of --passes is ~18 frames, and
// passes whose work varies by frame -- SDFGI, probe updates -- need more than that to compare two builds).
const ROUNDS = +arg('rounds', 1);
const rows = [];
let f = 0, frames = 0, wall = 0, copies = 0, timeline = 0, r = null;
for (let round = 0; round < ROUNDS; round++) {
	await page.evaluate(() => { const G = window.__gt; Object.assign(G, { used: 0, meta: [], frames: 0, copies: 0, t1: 0, on: true, t0: performance.now() }); });
	await page.waitForFunction(() => window.__gt.t1 > 0, null, { timeout: 60000, polling: 100 });
	r = await page.evaluate(async () => {
		const G = window.__gt; G.on = false;
		await new Promise((res) => setTimeout(res, 300)); // let the last measured submits land
		const n = G.used * 2;
		const resolve = G.device.createBuffer({ size: n * 8, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
		const rb = G.device.createBuffer({ size: n * 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
		const e = G.device.createCommandEncoder();
		e.resolveQuerySet(G.qs, 0, n, resolve, 0);
		e.copyBufferToBuffer(resolve, 0, rb, 0, n * 8);
		G.device.queue.submit([e.finish()]);
		await rb.mapAsync(GPUMapMode.READ);
		const ts = new BigInt64Array(rb.getMappedRange().slice(0));
		rb.destroy(); resolve.destroy();
		const spans = [];
		for (let k = 0; k < G.used; k++) spans.push([Number(ts[2 * k]), Number(ts[2 * k + 1])]);
		const sh = window.godotWebGPUShaderStats;
		return { spans, meta: G.meta, copies: G.copies, frames: G.frames, wall: G.t1 - G.t0, stats: window.godotWebGPUFrameStats || null, shaders: sh ? { baked: sh.baked, precompiled: sh.precompiled, translated: sh.translated } : null };
	});
	// Whole frames only: drop the partial frame the measurement stopped in.
	const lastFrame = r.meta.length ? r.meta[r.meta.length - 1].frame : 0;
	const rr = r.spans.map((s, k) => ({ s, m: r.meta[k] })).filter(({ s: [a, b], m }) => a > 0 && b >= a && (!PASSES || m.frame < lastFrame));
	rows.push(...rr);
	f += PASSES ? Math.max(lastFrame - r.meta[0].frame, 1) : Math.max(r.frames, 1);
	frames += r.frames; wall += r.wall; copies += r.copies;
	timeline += (Math.max(...rr.map((x) => x.s[1])) - Math.min(...rr.map((x) => x.s[0]))) / 1e6;
}
const busy = rows.reduce((t, { s: [a, b] }) => t + (b - a), 0) / 1e6;
console.log(`frames ${f} over ${wall.toFixed(0)} ms (${ROUNDS} round(s)) -> ${(1000 * frames / wall).toFixed(0)} fps, ${(wall / frames).toFixed(2)} ms/frame`);
console.log(`GPU busy ${(busy / f).toFixed(2)} ms/frame (${(100 * busy / timeline).toFixed(0)}% of the GPU timeline), ${rows.length} ${PASSES ? 'passes' : 'command buffers'} timed`);
if (PASSES) {
	console.log(`copyTextureToTexture ${(copies / frames).toFixed(1)}/frame (copies are timed as 'copy' rows)`);
	const agg = {};
	for (const { s: [a, b], m } of rows) {
		const key = `${m.type.padEnd(7)} ${m.pipes.slice(0, 2).join(',').slice(0, 60).padEnd(60)} -> ${String(m.att).slice(0, 40)}${DIMS && m.dims.length ? ' groups ' + m.dims.slice(0, 3).join(',') : ''}`;
		const e = agg[key] || (agg[key] = { ms: 0, c: 0, n: 0 }); e.ms += (b - a) / 1e6; e.c++; e.n += m.n;
	}
	const TOP = +arg('top', 30);
	console.log('ms/frame  passes/f  draws|dispatches/f  pipelines -> first attachment');
	for (const [k, v] of Object.entries(agg).sort((x, y) => y[1].ms - x[1].ms).slice(0, TOP)) console.log(`${(v.ms / f).toFixed(3).padStart(8)} ${(v.c / f).toFixed(1).padStart(9)} ${(v.n / f).toFixed(0).padStart(10)}  ${k}`);
} else {
	const per = rows.map(({ s: [a, b] }) => (b - a) / 1e6).sort((x, y) => x - y);
	console.log(`per command buffer: median ${per[per.length >> 1].toFixed(3)} ms, p95 ${per[Math.floor(per.length * 0.95)].toFixed(3)} ms, max ${per[per.length - 1].toFixed(3)} ms`);
}
if (r.stats) console.log('engine frame stats: ' + JSON.stringify(r.stats));
if (r.shaders) console.log('shader sources: ' + JSON.stringify(r.shaders));
await browser.close(); server.close();
