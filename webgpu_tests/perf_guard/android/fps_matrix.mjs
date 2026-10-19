// On-phone fps for each exported variant (main-thread rendering): cold Chrome, one tab,
// boot detected by the engine's first GPUQueue.submit on the page, then settle + measure.
// Reports fps, frame-time percentiles, main-thread busy %, WebGPU calls/frame, renderer.
//   node matrix_fps.mjs --root <exp dir> --variants a,b,c [--settle 25000] [--measure 10000]
import { serve, CDP, targets, sleep } from './cdp.mjs';
import { execFileSync } from 'child_process';
import { resolve, join } from 'path';
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const ROOT = resolve(arg('root')); const VARIANTS = arg('variants').split(',');
const SETTLE = +arg('settle', 25000), MEASURE = +arg('measure', 10000), PORT = +arg('port', 8099);
const adb = (...x) => execFileSync('adb', x, { encoding: 'utf8' });
const T0 = Date.now(); const ts = () => `t+${((Date.now() - T0) / 1000).toFixed(0)}s`;
const step = (v, m) => console.log(`  [${v}] ${ts()} ${m}`);
const results = [];
for (const v of VARIANTS) {
	const server = await serve(join(ROOT, v), PORT);
	adb('shell', 'am', 'force-stop', 'com.android.chrome'); await sleep(1500);
	adb('shell', 'am', 'start', '-a', 'android.intent.action.VIEW', '-d', `http://localhost:${PORT}/__blank`, 'com.android.chrome');
	let blank = null;
	for (let i = 0; i < 90 && !blank; i++) {
		await sleep(1000);
		try { adb('forward', 'tcp:9222', 'localabstract:chrome_devtools_remote'); } catch {}
		let list; try { list = await targets(); } catch { continue; }
		blank = list.find((t) => t.type === 'page' && t.url.includes('__blank'));
		if (blank) for (const t of list) if (t.type === 'page' && t.id !== blank.id) { try { await fetch(`http://127.0.0.1:9222/json/close/${t.id}`); } catch {} }
	}
	if (!blank) { console.log(`  [${v}] blank page never appeared, skipping`); server.close(); continue; }
	step(v, 'chrome up, other tabs closed');
	const c = await CDP.attach(blank.webSocketDebuggerUrl);
	await c.send('Runtime.enable'); await c.send('Page.enable'); await c.send('Performance.enable');
	const logs = [];
	c.on('Runtime.consoleAPICalled', (p) => logs.push((p.args || []).map((x) => x.value ?? x.description ?? '').join(' ')));
	await c.send('Page.addScriptToEvaluateOnNewDocument', { source: `(function(){
		const F = globalThis.__f = { n: 0, t: 0, times: [], submits: 0, calls: 0 };
		const r = requestAnimationFrame.bind(globalThis); let last = 0;
		globalThis.requestAnimationFrame = (cb) => r((t) => { if (F.t) { F.n++; if (last) F.times.push(t - last); } last = t; cb(t); });
		const wrap = (cls) => { const proto = globalThis[cls] && globalThis[cls].prototype; if (!proto) return;
			for (const k of Object.getOwnPropertyNames(proto)) { const d = Object.getOwnPropertyDescriptor(proto, k);
				if (k === 'constructor' || !d || typeof d.value !== 'function') continue; const f = d.value;
				proto[k] = function (...a) { if (F.t) F.calls++; return f.apply(this, a); }; } };
		for (const cls of ['GPUDevice','GPUCommandEncoder','GPURenderPassEncoder','GPUComputePassEncoder','GPUQueue','GPUBuffer','GPUTexture','GPUCanvasContext']) wrap(cls);
		const s = GPUQueue.prototype.submit; GPUQueue.prototype.submit = function (b) { F.submits++; return s.call(this, b); };
	})();` });
	const tNav = Date.now();
	await c.send('Page.navigate', { url: `http://localhost:${PORT}/index.html` });
	let booted = false;
	for (let i = 0; i < 120 && !booted; i++) { await sleep(1000); try { booted = (await c.eval('globalThis.__f ? globalThis.__f.submits : 0')) > 30; } catch {} }
	const bootS = (Date.now() - tNav) / 1000;
	step(v, booted ? `engine submitting (boot ${bootS.toFixed(0)}s), settling ${SETTLE / 1000}s` : `no GPU submits on the page after 120s (worker rendering or boot failure), measuring anyway`);
	await sleep(SETTLE);
	await c.eval(`Object.assign(globalThis.__f, { n: 0, times: [], calls: 0, t: performance.now() });`);
	const m0 = await c.send('Performance.getMetrics'); await sleep(MEASURE); const m1 = await c.send('Performance.getMetrics');
	const pick = (m, n) => (m.metrics.find((x) => x.name === n) || {}).value;
	const busy = 100 * (pick(m1, 'TaskDuration') - pick(m0, 'TaskDuration')) / (pick(m1, 'Timestamp') - pick(m0, 'Timestamp'));
	const r = JSON.parse(await c.eval(`(() => { const F = globalThis.__f, cv = document.querySelector('canvas'); const t = F.times.slice().sort((a,b)=>a-b);
		const q = (p) => t.length ? t[Math.min(t.length-1, Math.floor(t.length*p))] : 0; const dt = performance.now() - F.t;
		return JSON.stringify({ fps: 1000*F.n/dt, p50: q(.5), p90: q(.9), p99: q(.99), calls: F.calls / Math.max(F.n,1), canvas: cv ? [cv.width, cv.height] : null }); })()`));
	const banner = logs.find((l) => /WebGPU 1\.0 - /.test(l));
	const row = { v, renderer: (banner || 'no banner').replace(/.*WebGPU 1\.0 - /, '').split(' - ')[0], ...r, busy, bootS, booted,
		errors: logs.filter((l) => /ERROR|error:|Uncaught|failed/i.test(l)).length };
	results.push(row);
	console.log(`${v.padEnd(22)} ${row.renderer.padEnd(9)} fps ${r.fps.toFixed(1).padStart(5)}  p50 ${r.p50.toFixed(1).padStart(5)} p90 ${r.p90.toFixed(1).padStart(5)} p99 ${r.p99.toFixed(1).padStart(6)} ms  main ${busy.toFixed(0).padStart(3)}%  gpucalls/f ${r.calls.toFixed(0).padStart(4)}  canvas ${r.canvas}  boot ${bootS.toFixed(0)}s  errs ${row.errors}`);
	c.close(); server.close();
}
console.log('\nJSON ' + JSON.stringify(results));
process.exit(0);
