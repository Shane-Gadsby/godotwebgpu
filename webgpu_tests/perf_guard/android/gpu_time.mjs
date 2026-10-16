// gpu_time.mjs, but against Chrome on a USB-connected Android phone over CDP.
// The instrumentation is lifted verbatim out of gpu_time.mjs at runtime (the
// addInitScript body), so mobile numbers stay comparable with the desktop tool.
//
//   node gpu_mobile.mjs --dir <export dir> [--passes] [--top 30] [--settle 35000] [--rounds 1]
import { serve, CDP, targets, sleep } from './cdp.mjs';
import { readFileSync, writeFileSync, mkdirSync } from 'fs';
import { execFileSync } from 'child_process';
import { resolve, join, dirname } from 'path';
import { fileURLToPath } from 'url';

const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const PORT = +arg('port', 8099);
const SETTLE = +arg('settle', 35000);
const ROUNDS = +arg('rounds', 1);
const PASSES = process.argv.includes('--passes');
const TOP = +arg('top', 30);
const HERE = dirname(fileURLToPath(import.meta.url));
const GPU_TIME = arg('gpu-time', join(HERE, '..', 'gpu_time.mjs'));
// --replace-wgsl <label substring>=<file>: substitute a shader module's WGSL (bisecting a slow shader by hand-editing it).
// --dump-wgsl <label substring>: save every matching module's WGSL to --dump-dir (default ./wgsl_dump) as <label>.wgsl.
const REPLACE = process.argv.flatMap((a, i) => (a === '--replace-wgsl' ? [process.argv[i + 1]] : [])).map((x) => { const j = x.indexOf('='); return [x.slice(0, j), readFileSync(x.slice(j + 1), 'utf8')]; });
const DUMP = arg('dump-wgsl', ''); const DUMP_DIR = resolve(arg('dump-dir', 'wgsl_dump'));
// --replace-pair <orig.wgsl>=<new.wgsl>: substitute only the module whose code is exactly orig (several modules can share a label).
const PAIRS = process.argv.flatMap((a, i) => (a === '--replace-pair' ? [process.argv[i + 1]] : [])).map((x) => { const j = x.indexOf('='); return [readFileSync(x.slice(0, j), 'utf8'), readFileSync(x.slice(j + 1), 'utf8')]; });
// --f16: also request the shader-f16 device feature, so a --replace-wgsl module may use `enable f16;`.
const F16 = process.argv.includes('--f16');
// --log-pipelines <label substring>: print each matching render pipeline's label and override constants (are the
// engine's specialization constants actually reaching Dawn, or is every feature path compiled live?).
const LOGP = arg('log-pipelines', '');

// Lift the init-script body out of gpu_time.mjs rather than duplicating it.
const src = readFileSync(GPU_TIME, 'utf8');
const START = 'await page.addInitScript((PASSES) => {';
const END = '}, PASSES);';
const a = src.indexOf(START);
const b = src.indexOf(END, a);
if (a === -1 || b === -1) { console.error('could not locate the addInitScript block in gpu_time.mjs'); process.exit(1); }
const body = src.slice(a + START.length, b);
const initScript = `(function(){ const PASSES = ${PASSES}; ${body} })();`;
console.log(`[init] reused ${body.length} B of instrumentation from gpu_time.mjs (PASSES=${PASSES})`);

await serve(DIR, PORT);
const adb = (...x) => execFileSync('adb', x, { encoding: 'utf8' });

adb('shell', 'am', 'force-stop', 'com.android.chrome');
await sleep(1500);
// Land on a blank page first: the init script has to be installed before the
// engine's page runs, and CDP can only attach once Chrome is up.
adb('shell', 'am', 'start', '-a', 'android.intent.action.VIEW',
	'-d', `http://localhost:${PORT}/__blank`, 'com.android.chrome');

let tgt = null;
for (let i = 0; i < 90; i++) {
	await sleep(1000);
	try { adb('forward', 'tcp:9222', 'localabstract:chrome_devtools_remote'); } catch {}
	let list; try { list = await targets(); } catch { continue; }
	tgt = list.find((t) => t.type === 'page' && t.url.includes(`:${PORT}`));
	if (tgt) break;
}
if (!tgt) { console.error('page target never appeared'); process.exit(1); }
// Chrome restores old tabs on relaunch; a second engine tab would share the GPU.
for (const t of await targets()) if (t.type === 'page' && t.id !== tgt.id) { try { await fetch(`http://127.0.0.1:9222/json/close/${t.id}`); } catch {} }

const c = await CDP.attach(tgt.webSocketDebuggerUrl);
await c.send('Runtime.enable');
await c.send('Page.enable');
await c.send('Log.enable');
const logs = [];
c.on('Log.entryAdded', (p) => { if (p.entry.level === 'error') console.log('  [page error] ' + p.entry.text.slice(0, 200)); });
c.on('Runtime.consoleAPICalled', (p) => {
	const t = (p.args || []).map((x) => x.value ?? x.description ?? '').join(' ');
	logs.push(t); if (t.startsWith('[gpu_time]') || p.type === 'error') console.log('  ' + t.slice(0, 220));
});
c.on('Log.entryAdded', (p) => logs.push(`[${p.entry.level}] ${p.entry.text}`));

if (F16) await c.send('Page.addScriptToEvaluateOnNewDocument', { source: `(function () {
	const rd = GPUAdapter.prototype.requestDevice;
	GPUAdapter.prototype.requestDevice = async function (desc = {}) {
		const d = { ...desc };
		if (this.features.has('shader-f16')) d.requiredFeatures = [...new Set([...(desc.requiredFeatures || []), 'shader-f16'])];
		else console.log('[gpu_time] adapter has no shader-f16');
		return rd.call(this, d);
	};
})();` });
await c.send('Page.addScriptToEvaluateOnNewDocument', { source: `(function (REPLACE, DUMP, PAIRS) {
	globalThis.__wgsl = {};
	const cs = GPUDevice.prototype.createShaderModule;
	GPUDevice.prototype.createShaderModule = function (d) {
		const label = (d && d.label) || '';
		if (DUMP && label.includes(DUMP)) { const a = globalThis.__wgsl[label] || (globalThis.__wgsl[label] = []); if (!a.includes(d.code)) a.push(d.code); }
		const pr = PAIRS.find(([orig]) => orig === d.code);
		const r = pr ? [label, pr[1]] : (label && REPLACE.find(([sub]) => label.includes(sub)));
		if (r) console.log('[gpu_time] replaced WGSL of ' + label + (pr ? ' (exact match)' : ' (label match)'));
		return cs.call(this, r ? { ...d, code: r[1] } : d);
	};
})(${JSON.stringify(REPLACE)}, ${JSON.stringify(DUMP)}, ${JSON.stringify(PAIRS)});` });
if (LOGP) await c.send('Page.addScriptToEvaluateOnNewDocument', { source: `(function (LOGP) {
	const seen = new Set();
	const cp = GPUDevice.prototype.createRenderPipeline;
	GPUDevice.prototype.createRenderPipeline = function (d) {
		const label = (d && d.label) || '';
		if (label.includes(LOGP)) {
			const key = label + JSON.stringify(d.fragment && d.fragment.constants) + JSON.stringify(d.vertex && d.vertex.constants);
			if (!seen.has(key)) { seen.add(key); console.log('[gpu_time] pipeline ' + label + ' vs.constants=' + JSON.stringify(d.vertex && d.vertex.constants) + ' fs.constants=' + JSON.stringify(d.fragment && d.fragment.constants) + ' fs.module=' + ((d.fragment && d.fragment.module && d.fragment.module.label) || '?')); }
		}
		return cp.call(this, d);
	};
})(${JSON.stringify(LOGP)});` });
await c.send('Page.addScriptToEvaluateOnNewDocument', { source: initScript });
console.log('[cdp] instrumentation installed; navigating to the export');
await c.send('Page.navigate', { url: `http://localhost:${PORT}/index.html` });

console.log(`[wait] settling ${SETTLE / 1000}s...`);
await sleep(SETTLE);

if (DUMP) {
	const mods = await c.eval('JSON.stringify(globalThis.__wgsl)');
	const obj = JSON.parse(mods); mkdirSync(DUMP_DIR, { recursive: true });
	for (const [label, codes] of Object.entries(obj)) codes.forEach((code, k) => { const f = join(DUMP_DIR, label.replace(/[^A-Za-z0-9_.:#-]/g, '_') + `#${k}.wgsl`); writeFileSync(f, code); console.log(`[dump] ${label} #${k} -> ${f} (${code.length} B)`); });
	if (!Object.keys(obj).length) console.log('[dump] no module label contained ' + JSON.stringify(DUMP));
	if (process.argv.includes('--dump-only')) { c.close(); process.exit(0); }
}
const has = await c.eval('!!(window.__gt && window.__gt.qs)');
if (!has) { console.error('instrumentation did not attach (no query set) — adapter may lack timestamp-query'); process.exit(1); }

const rows = []; let f = 0, frames = 0, wall = 0, copies = 0, timeline = 0, r = null;
for (let round = 0; round < ROUNDS; round++) {
	await c.eval(`(() => { const G = window.__gt; Object.assign(G, { used: 0, meta: [], frames: 0, copies: 0, t1: 0, on: true, t0: performance.now() }); })()`);
	let ok = false;
	for (let i = 0; i < 1200; i++) { await sleep(250); if (await c.eval('window.__gt.t1 > 0')) { ok = true; break; }
		if (i % 40 === 39) console.log('  [fill] ' + await c.eval('JSON.stringify({used: window.__gt.used, frames: window.__gt.frames})')); }
	if (!ok) { console.error('query set never filled — is the page rendering?'); process.exit(1); }
	r = await c.eval(`(async () => {
		const G = window.__gt; G.on = false;
		await new Promise((res) => setTimeout(res, 300));
		const n = G.used * 2;
		const resolveBuf = G.device.createBuffer({ size: n * 8, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
		const rb = G.device.createBuffer({ size: n * 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
		const e = G.device.createCommandEncoder();
		e.resolveQuerySet(G.qs, 0, n, resolveBuf, 0);
		e.copyBufferToBuffer(resolveBuf, 0, rb, 0, n * 8);
		G.device.queue.submit([e.finish()]);
		await rb.mapAsync(GPUMapMode.READ);
		const ts = new BigInt64Array(rb.getMappedRange().slice(0));
		rb.destroy(); resolveBuf.destroy();
		const spans = [];
		for (let k = 0; k < G.used; k++) spans.push([Number(ts[2 * k]), Number(ts[2 * k + 1])]);
		const sh = window.godotWebGPUShaderStats;
		return { spans, meta: G.meta, copies: G.copies, frames: G.frames, wall: G.t1 - G.t0,
			stats: window.godotWebGPUFrameStats || null,
			shaders: sh ? { baked: sh.baked, precompiled: sh.precompiled, translated: sh.translated } : null };
	})()`, true);
	const lastFrame = r.meta.length ? r.meta[r.meta.length - 1].frame : 0;
	const rr = r.spans.map((s, k) => ({ s, m: r.meta[k] })).filter(({ s: [x, y], m }) => x > 0 && y >= x && (!PASSES || m.frame < lastFrame));
	rows.push(...rr);
	f += PASSES ? Math.max(lastFrame - r.meta[0].frame, 1) : Math.max(r.frames, 1);
	frames += r.frames; wall += r.wall; copies += r.copies;
	timeline += (Math.max(...rr.map((x) => x.s[1])) - Math.min(...rr.map((x) => x.s[0]))) / 1e6;
}
const busy = rows.reduce((t, { s: [x, y] }) => t + (y - x), 0) / 1e6;
console.log('\n==================== GPU TIME (on device) ====================');
console.log(`frames ${f} over ${wall.toFixed(0)} ms -> ${(1000 * frames / wall).toFixed(1)} fps, ${(wall / frames).toFixed(2)} ms/frame`);
console.log(`GPU busy ${(busy / f).toFixed(2)} ms/frame (${(100 * busy / timeline).toFixed(0)}% of the GPU timeline), ${rows.length} ${PASSES ? 'passes' : 'command buffers'} timed`);
if (PASSES) {
	console.log(`copyTextureToTexture ${(copies / frames).toFixed(1)}/frame`);
	const agg = {};
	for (const { s: [x, y], m } of rows) {
		const key = `${String(m.type).padEnd(7)} ${m.pipes.slice(0, 2).join(',').slice(0, 58).padEnd(58)} -> ${String(m.att).slice(0, 38)}`;
		const e = agg[key] || (agg[key] = { ms: 0, c: 0, n: 0 }); e.ms += (y - x) / 1e6; e.c++; e.n += m.n;
	}
	console.log('ms/frame  passes/f  draws|disp/f  pipelines -> first attachment');
	for (const [k, v] of Object.entries(agg).sort((p, q) => q[1].ms - p[1].ms).slice(0, TOP))
		console.log(`${(v.ms / f).toFixed(3).padStart(8)} ${(v.c / f).toFixed(1).padStart(9)} ${(v.n / f).toFixed(0).padStart(12)}  ${k}`);
} else {
	const per = rows.map(({ s: [x, y] }) => (y - x) / 1e6).sort((p, q) => p - q);
	console.log(`per command buffer: median ${per[per.length >> 1].toFixed(3)} ms, p95 ${per[Math.floor(per.length * 0.95)].toFixed(3)} ms, max ${per[per.length - 1].toFixed(3)} ms`);
}
if (r.stats) console.log('engine frame stats: ' + JSON.stringify(r.stats));
if (r.shaders) console.log('shader sources: ' + JSON.stringify(r.shaders));
c.close(); process.exit(0);
