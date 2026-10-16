// Boot the export on the phone and report: adapter/limits, which renderer the engine picked,
// canvas/backbuffer size, and steady-state fps. Nothing is changed on the device.
import { serve, CDP, targets, sleep } from './cdp.mjs';
import { execFileSync } from 'child_process';

const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = arg('dir');
const PORT = +arg('port', 8099);
const SETTLE = +arg('settle', 30000);
const MEASURE = +arg('measure', 8000);
const URL_PATH = arg('path', '/index.html');

await serve(DIR, PORT);
console.log(`[serve] ${DIR} on 127.0.0.1:${PORT} (phone reaches it via adb reverse)`);

const adb = (...a) => execFileSync('adb', a, { encoding: 'utf8' });

// A fresh tab each run: a reload would reuse the warm shader cache and the already-JITted wasm.
adb('shell', 'am', 'force-stop', 'com.android.chrome');
await sleep(1500);
adb('shell', 'am', 'start', '-a', 'android.intent.action.VIEW',
	'-d', `http://localhost:${PORT}${URL_PATH}`, 'com.android.chrome');
console.log('[adb] launched Chrome');

// Wait for the page target to appear. force-stop destroyed the abstract devtools
// socket, so the forward has to be rebound against the new Chrome process, and
// /json/list refuses connections until it is listening again.
let tgt = null;
for (let i = 0; i < 90; i++) {
	await sleep(1000);
	try { adb('forward', 'tcp:9222', 'localabstract:chrome_devtools_remote'); } catch {}
	let list;
	try { list = await targets(); } catch { continue; }
	tgt = list.find((t) => t.type === 'page' && t.url.includes(`:${PORT}`));
	if (tgt) break;
}
if (!tgt) { console.error('page target never appeared'); process.exit(1); }
console.log(`[cdp] attaching to ${tgt.url}`);

const c = await CDP.attach(tgt.webSocketDebuggerUrl);
await c.send('Runtime.enable');
await c.send('Log.enable');
await c.send('Page.enable');
await c.send('Performance.enable');

const logs = [];
c.on('Runtime.consoleAPICalled', (p) => {
	const text = (p.args || []).map((a) => a.value ?? a.description ?? '').join(' ');
	logs.push(text);
});
c.on('Log.entryAdded', (p) => logs.push(`[${p.entry.level}] ${p.entry.text}`));

console.log(`[wait] settling ${SETTLE / 1000}s for engine boot + shader warmup...`);
await sleep(SETTLE);

// Install an fps counter, then sample.
await c.eval(`
	window.__pf = { frames: 0, times: [] };
	(function () {
		const raf = window.requestAnimationFrame.bind(window);
		let last = performance.now();
		const tick = (t) => { window.__pf.frames++; window.__pf.times.push(t - last); last = t; raf(tick); };
		raf(tick);
	})();
`);

const m0 = await c.send('Performance.getMetrics');
await sleep(MEASURE);
const m1 = await c.send('Performance.getMetrics');

const pick = (ms, n) => (ms.metrics.find((x) => x.name === n) || {}).value;
const dTask = pick(m1, 'TaskDuration') - pick(m0, 'TaskDuration');
const dTs = pick(m1, 'Timestamp') - pick(m0, 'Timestamp');

const pf = await c.eval(`(() => {
	const t = window.__pf.times.slice(-400).filter((x) => x > 0).sort((a, b) => a - b);
	const q = (p) => t.length ? t[Math.min(t.length - 1, Math.floor(t.length * p))] : null;
	return { frames: window.__pf.frames, n: t.length, p50: q(0.5), p90: q(0.9), p99: q(0.99) };
})()`);

const info = await c.eval(`(async () => {
	const out = {};
	out.dpr = window.devicePixelRatio;
	out.inner = [window.innerWidth, window.innerHeight];
	const cv = document.querySelector('canvas');
	if (cv) { out.canvasAttr = [cv.width, cv.height]; out.canvasCss = [cv.clientWidth, cv.clientHeight]; }
	out.hasGPU = !!navigator.gpu;
	if (navigator.gpu) {
		const ad = await navigator.gpu.requestAdapter();
		if (ad) {
			out.adapterInfo = { vendor: ad.info?.vendor, architecture: ad.info?.architecture, device: ad.info?.device, description: ad.info?.description };
			const L = {};
			for (const k of ['maxSampledTexturesPerShaderStage','maxStorageTexturesPerShaderStage','maxTextureDimension2D','maxBindGroups','maxComputeWorkgroupStorageSize','maxUniformBufferBindingSize','maxStorageBufferBindingSize','maxColorAttachments','maxBufferSize']) L[k] = ad.limits[k];
			out.adapterLimits = L;
			out.features = [...ad.features].sort();
		}
	}
	return out;
})()`, true);

console.log('\n==================== DEVICE / PAGE ====================');
console.log(JSON.stringify(info, null, 2));
console.log('\n==================== FRAME TIME ====================');
console.log(`main-thread busy : ${(100 * dTask / dTs).toFixed(1)} %   (~100% = CPU-bound on the main thread)`);
console.log(`fps (rAF)        : ${(pf.frames / (MEASURE / 1000)).toFixed(1)}`);
console.log(`frame ms  p50     : ${pf.p50?.toFixed(2)}   p90 ${pf.p90?.toFixed(2)}   p99 ${pf.p99?.toFixed(2)}   (n=${pf.n})`);

console.log('\n==================== ENGINE LOG (filtered) ====================');
const keep = logs.filter((l) => /Vulkan|WebGPU|adapter|Forward|Mobile|Clustered|renderer|shader|error|Error|ERROR|warn|WARN|fail|texture|limit/i.test(l));
for (const l of keep.slice(0, 80)) console.log('  ' + l.slice(0, 300));
console.log(`\n(${logs.length} console lines total, ${keep.length} matched)`);
c.close();
process.exit(0);
