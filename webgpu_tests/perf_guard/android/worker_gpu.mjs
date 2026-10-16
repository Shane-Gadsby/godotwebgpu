// On-device GPU timing for an export whose render thread is a pthread worker.
// That worker never returns to its event loop long enough to answer Runtime.evaluate,
// so the whole measurement is self-driven inside it (started by its own queue.submit
// hook, finished when the query set is full) and reported through console.log, which
// the worker's inspector delivers synchronously. gpu_time.mjs's init block is reused.
//
//   node worker_gpu.mjs --dir <export dir> [--passes] [--top 30] [--settle 40000] [--warmup 60] [--wait 300000]
import { serve, CDP, targets, sleep } from './cdp.mjs';
import { readFileSync } from 'fs';
import { execFileSync } from 'child_process';
import { resolve, join, dirname } from 'path';
import { fileURLToPath } from 'url';

const T0 = Date.now();
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const PORT = +arg('port', 8099);
const SETTLE = +arg('settle', 40000);
const WARMUP = +arg('warmup', 60);     // submits to skip before measuring (shader warmup on a cold tab)
const WAIT = +arg('wait', 300000);     // how long to wait for the in-worker result after settle
const PASSES = process.argv.includes('--passes');
const TOP = +arg('top', 30);
const HERE = dirname(fileURLToPath(import.meta.url));
const GPU_TIME = arg('gpu-time', join(HERE, '..', 'gpu_time.mjs'));

const src = readFileSync(GPU_TIME, 'utf8');
const START = 'await page.addInitScript((PASSES) => {', END = '}, PASSES);';
const a = src.indexOf(START), b = src.indexOf(END, a);
if (a === -1 || b === -1) { console.error('could not find addInitScript block'); process.exit(1); }
const body = src.slice(a + START.length, b);

// The autonomous driver: counts submits, arms the instrumentation after WARMUP, and when
// the query set is full resolves it and logs the result in 8 KB chunks.
const DRIVER = `
(function () {
	const G = globalThis.__gt; if (!G) return;
	const log = (s) => console.log('[gpu_time] ' + s);
	let submits = 0, armed = false, finishing = false;
	const os = GPUQueue.prototype.submit;
	GPUQueue.prototype.submit = function (bufs) {
		const r = os.call(this, bufs);
		if (this !== (G.device && G.device.queue)) return r;
		submits++;
		if (!armed && submits >= ${WARMUP} && G.qs) {
			armed = true; Object.assign(G, { used: 0, meta: [], frames: 0, copies: 0, t1: 0, on: true, t0: performance.now() });
			log('armed at submit ' + submits + ', frames so far ' + G.frames);
		}
		if (armed && !finishing && submits % 200 === 0) log('fill used=' + G.used + ' frames=' + G.frames + ' submits=' + submits);
		if (armed && !finishing && G.t1 > 0) {
			finishing = true; G.on = false;
			log('full: used=' + G.used + ' frames=' + G.frames + ' wall=' + (G.t1 - G.t0).toFixed(0) + ' ms, resolving');
			const n = G.used * 2;
			const rs = G.device.createBuffer({ size: n * 8, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
			const rb = G.device.createBuffer({ size: n * 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
			const e = G.device.createCommandEncoder();
			e.resolveQuerySet(G.qs, 0, n, rs, 0); e.copyBufferToBuffer(rs, 0, rb, 0, n * 8);
			os.call(this, [e.finish()]);
			rb.mapAsync(GPUMapMode.READ).then(() => {
				const ts = new BigInt64Array(rb.getMappedRange().slice(0));
				rb.destroy(); rs.destroy();
				const spans = []; for (let k = 0; k < G.used; k++) spans.push([Number(ts[2 * k]), Number(ts[2 * k + 1])]);
				const json = JSON.stringify({ spans, meta: G.meta, copies: G.copies, frames: G.frames, wall: G.t1 - G.t0, submits,
					stats: globalThis.godotWebGPUFrameStats || null });
				const CH = 8192, parts = Math.ceil(json.length / CH);
				for (let i = 0; i < parts; i++) log('RESULT ' + i + '/' + parts + ' ' + json.slice(i * CH, (i + 1) * CH));
			}).catch((err) => log('map failed: ' + err));
		}
		return r;
	};
	const orf = globalThis.requestAnimationFrame;
	log('driver installed (rAF ' + (typeof orf) + ')');
})();`;
const INIT = `(function(){ const PASSES = ${PASSES}; const window = globalThis; ${body} })(); ${DRIVER}`;
console.log(`[init] reused ${body.length} B from gpu_time.mjs (PASSES=${PASSES}); autonomous driver appended`);

await serve(DIR, PORT);
const adb = (...x) => execFileSync('adb', x, { encoding: 'utf8' });
adb('shell', 'am', 'force-stop', 'com.android.chrome');
await sleep(1500);
adb('shell', 'am', 'start', '-a', 'android.intent.action.VIEW', '-d', `http://localhost:${PORT}/__blank`, 'com.android.chrome');
let list;
for (let i = 0; i < 90; i++) {
	await sleep(1000);
	try { adb('forward', 'tcp:9222', 'localabstract:chrome_devtools_remote'); } catch {}
	try { list = await targets(); break; } catch {}
}
const blank = list.find((t) => t.type === 'page' && t.url.includes('__blank'));
for (const t of list) if (t.type === 'page' && t.id !== blank.id) { try { await fetch(`http://127.0.0.1:9222/json/close/${t.id}`); } catch {} }
await sleep(1500);

const c = await CDP.attach(blank.webSocketDebuggerUrl);
await c.send('Runtime.enable'); await c.send('Page.enable');

// Result assembly from console lines on any session (worker or page).
const chunks = new Map(); let parts = 0; let resultJson = null; let resolveDone;
const done = new Promise((r) => { resolveDone = r; });
const onConsole = (p, sessionId) => {
	const t = (p.args || []).map((x) => x.value ?? x.description ?? '').join(' ');
	if (!t.startsWith('[gpu_time] ')) return;
	const m = t.match(/^\[gpu_time\] RESULT (\d+)\/(\d+) (.*)$/s);
	if (!m) { console.log(`  (${(sessionId || 'page').slice(0, 6)}) ${t.slice(0, 160)}`); return; }
	parts = +m[2]; chunks.set(+m[1], m[3]);
	if (chunks.size === parts) { resultJson = Array.from({ length: parts }, (_, i) => chunks.get(i)).join(''); resolveDone(); }
};
c.on('Runtime.consoleAPICalled', onConsole);

let injected = 0;
c.on('Target.attachedToTarget', async (p) => {
	const { sessionId, targetInfo, waitingForDebugger } = p;
	if (targetInfo.type === 'worker' || targetInfo.type === 'shared_worker') {
		try { await c.send('Runtime.enable', {}, sessionId); await c.send('Runtime.evaluate', { expression: INIT }, sessionId); injected++; }
		catch (e) { console.log('  [worker inject failed] ' + e.message); }
	}
	if (waitingForDebugger) { try { await c.send('Runtime.runIfWaitingForDebugger', {}, sessionId); } catch {} }
});
await c.send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: true, flatten: true });
await c.send('Page.addScriptToEvaluateOnNewDocument', { source: `(function(){ globalThis.__f={n:0}; const r=requestAnimationFrame.bind(globalThis); globalThis.requestAnimationFrame=(cb)=>r((t)=>{globalThis.__f.n++;cb(t);}); })();` });
await c.send('Page.navigate', { url: `http://localhost:${PORT}/index.html` });
console.log(`[wait] settling ${SETTLE / 1000}s (t+${((Date.now() - T0) / 1000).toFixed(0)}s)`);
await sleep(SETTLE);
console.log(`[workers] ${injected} worker(s) instrumented; waiting up to ${WAIT / 1000}s for the in-worker result`);
await c.eval(`globalThis.__f.n = 0; globalThis.__f.t = performance.now();`);
await Promise.race([done, sleep(WAIT)]);
const pageFps = await c.eval(`1000 * globalThis.__f.n / (performance.now() - globalThis.__f.t)`);
if (!resultJson) { console.error(`no result from the render thread (parts ${chunks.size}/${parts}); page rAF ${pageFps.toFixed(1)} fps`); process.exit(1); }
const r = JSON.parse(resultJson);

const lastFrame = r.meta.length ? r.meta[r.meta.length - 1].frame : 0;
const rows = r.spans.map((s, k) => ({ s, m: r.meta[k] })).filter(({ s: [x, y], m }) => x > 0 && y >= x && (!PASSES || m.frame < lastFrame));
// Frames: rAF-counted if the worker has rAF, else submits (one submit per frame is the engine's steady state).
const frames = r.frames > 0 ? r.frames : r.submits;
const f = PASSES ? Math.max(lastFrame - (r.meta[0] ? r.meta[0].frame : 0), 1) : Math.max(frames, 1);
const timeline = (Math.max(...rows.map((x) => x.s[1])) - Math.min(...rows.map((x) => x.s[0]))) / 1e6;
const busy = rows.reduce((t, { s: [x, y] }) => t + (y - x), 0) / 1e6;
console.log('\n==================== GPU TIME (render thread, on device) ====================');
console.log(`render-thread frames ${r.frames} (submits ${r.submits}) over ${r.wall.toFixed(0)} ms -> ${(1000 * frames / r.wall).toFixed(1)} fps; page rAF ${pageFps.toFixed(1)} fps`);
console.log(`frame budget ${(r.wall / frames).toFixed(2)} ms/frame`);
console.log(`GPU busy ${(busy / f).toFixed(2)} ms/frame = ${(100 * busy / timeline).toFixed(0)}% of the GPU timeline  (${rows.length} ${PASSES ? 'passes' : 'command buffers'}, ${(rows.length / f).toFixed(1)}/frame)`);
if (PASSES) {
	console.log(`copyTextureToTexture ${(r.copies / f).toFixed(1)}/frame`);
	const agg = {};
	for (const { s: [x, y], m } of rows) {
		const key = `${String(m.type).padEnd(7)} ${m.pipes.slice(0, 2).join(',').slice(0, 54).padEnd(54)} -> ${String(m.att).slice(0, 36)}`;
		const e = agg[key] || (agg[key] = { ms: 0, c: 0, n: 0 }); e.ms += (y - x) / 1e6; e.c++; e.n += m.n;
	}
	console.log('\nms/frame  passes/f  draws|disp/f  pipelines -> first attachment');
	for (const [k, v] of Object.entries(agg).sort((p, q) => q[1].ms - p[1].ms).slice(0, TOP))
		console.log(`${(v.ms / f).toFixed(3).padStart(8)} ${(v.c / f).toFixed(1).padStart(9)} ${(v.n / f).toFixed(0).padStart(12)}  ${k}`);
} else {
	const per = rows.map(({ s: [x, y] }) => (y - x) / 1e6).sort((p, q) => p - q);
	console.log(`per command buffer: median ${per[per.length >> 1].toFixed(3)} ms, p95 ${per[Math.floor(per.length * 0.95)].toFixed(3)} ms, max ${per[per.length - 1].toFixed(3)} ms`);
}
if (r.stats) console.log('engine frame stats: ' + JSON.stringify(r.stats));
c.close(); process.exit(0);
