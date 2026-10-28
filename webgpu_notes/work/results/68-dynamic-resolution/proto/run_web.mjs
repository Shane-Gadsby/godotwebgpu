// Throwaway harness for task 68: load the dynres_proto export in the mandated browser launch, give the page a
// live GPU-time source (timestamp-query, what the engine would have with working driver timestamps) and a
// submit->onSubmittedWorkDone latency source (no feature needed), collect the [dr] lines.
//   node run_web.mjs --dir <export> --query "mode=sweep&up=bilinear" --out x.json [--browser firefox] [--size 1920x1080] [--timeout 200]
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync } from 'fs';
import { join, extname, resolve } from 'path';
const REPO = '/mnt/109313D2109313D2/godot-editors/godotwebgpu';
const { FIREFOX_WEBGPU_PREFS } = await import(join(REPO, 'webgpu_tests/scene_smoketest/firefox_prefs.mjs'));
const { chromium, firefox } = await import(join(REPO, 'webgpu_tests/scene_smoketest/node_modules/playwright/index.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const QUERY = arg('query', 'mode=sweep');
const OUT = arg('out', 'out.json');
const BROWSER = arg('browser', 'chrome');
const [W, H] = arg('size', '1920x1080').split('x').map(Number);
const TIMEOUT = +arg('timeout', 200) * 1000;
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const browser = BROWSER === 'firefox'
	? await firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS })
	: await chromium.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false,
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
		ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: W, height: H } });
const lines = [];
let adapter = null;
page.on('console', (m) => {
	const t = m.text();
	if (t.startsWith('[dr] ')) { const j = JSON.parse(t.slice(5)); lines.push(j); console.log(t.slice(0, 300)); }
	else if (t.startsWith('[drjs] ')) { console.log(t); if (t.includes('adapter')) adapter = t; }
	else if (/error|Mobile|Forward/i.test(t)) console.log('  page:', t.slice(0, 200));
});
await page.addInitScript(() => {
	const D = window.__dr = { qs: null, dev: null, used: 0, cap: 2048, enc: new WeakMap(), cbk: new WeakMap(), meta: [], res: null,
		harvested: [], raf: [], done_l: [], next_harvest: 0, harvesting: false };
	const ad = GPU.prototype.requestAdapter;
	GPU.prototype.requestAdapter = async function (...a) {
		const ada = await ad.apply(this, a);
		if (ada) {
			const i = ada.info || {};
			console.log(`[drjs] adapter ${i.vendor}/${i.architecture} fallback=${i.isFallbackAdapter} maxSampledTexturesPerShaderStage=${ada.limits.maxSampledTexturesPerShaderStage} timestamp-query=${ada.features.has('timestamp-query')} encoder.writeTimestamp=${typeof GPUCommandEncoder.prototype.writeTimestamp} features=${[...ada.features].join(',')}`);
		} else console.log('[drjs] adapter null');
		return ada;
	};
	const rd = GPUAdapter.prototype.requestDevice;
	GPUAdapter.prototype.requestDevice = async function (desc = {}) {
		const d = { ...desc };
		if (this.features.has('timestamp-query')) d.requiredFeatures = [...new Set([...(desc.requiredFeatures || []), 'timestamp-query'])];
		const dev = await rd.call(this, d);
		if (dev.features.has('timestamp-query')) {
			D.dev = dev;
			D.qs = dev.createQuerySet({ type: 'timestamp', count: D.cap * 2 });
			D.res = dev.createBuffer({ size: D.cap * 256, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
		}
		const q = dev.queue;
		const sub = q.submit;
		q.submit = function (cbs) {
			const t = performance.now();
			for (const cb of cbs) { const k = D.cbk.get(cb); if (k !== undefined) D.meta[k].t = t; }
			const r = sub.call(this, cbs);
			q.onSubmittedWorkDone().then(() => { D.done_l.push([t, performance.now() - t]); if (D.done_l.length > 4000) D.done_l.splice(0, 2000); });
			return r;
		};
		return dev;
	};
	const ce = GPUDevice.prototype.createCommandEncoder;
	GPUDevice.prototype.createCommandEncoder = function (...a) {
		const e = ce.apply(this, a);
		if (D.qs && this === D.dev && !D.harvesting) {
			const k = D.used++ % D.cap;
			D.enc.set(e, k);
			D.meta[k] = { t: 0, gen: D.used };
			e.beginComputePass({ timestampWrites: { querySet: D.qs, beginningOfPassWriteIndex: 2 * k } }).end();
		}
		return e;
	};
	const fin = GPUCommandEncoder.prototype.finish;
	GPUCommandEncoder.prototype.finish = function (...a) {
		const k = D.enc.get(this);
		if (k !== undefined) {
			this.beginComputePass({ timestampWrites: { querySet: D.qs, endOfPassWriteIndex: 2 * k + 1 } }).end();
			this.resolveQuerySet(D.qs, 2 * k, 2, D.res, k * 256);
		}
		const cb = fin.apply(this, a);
		if (k !== undefined) D.cbk.set(cb, k);
		return cb;
	};
	async function harvest() {
		if (!D.qs || D.harvesting) return;
		D.harvesting = true;
		const keys = [];
		for (let k = 0; k < D.cap; k++) { const m = D.meta[k]; if (m && m.t > 0 && !m.h) { m.h = true; keys.push([k, m.t]); } }
		const rb = D.dev.createBuffer({ size: D.cap * 256, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
		const e = D.dev.createCommandEncoder();
		e.copyBufferToBuffer(D.res, 0, rb, 0, D.cap * 256);
		D.dev.queue.submit([e.finish()]);
		await rb.mapAsync(GPUMapMode.READ);
		const v = new BigInt64Array(rb.getMappedRange());
		for (const [k, t] of keys) { const a = v[k * 32], b = v[k * 32 + 1]; if (a > 0n && b >= a) D.harvested.push([t, Number(b - a) / 1e6]); }
		rb.unmap(); rb.destroy();
		if (D.harvested.length > 20000) D.harvested.splice(0, 10000);
		D.harvesting = false;
	}
	const raf = window.requestAnimationFrame.bind(window);
	window.requestAnimationFrame = (cb) => raf((t) => {
		const n = performance.now();
		D.raf.push(n); if (D.raf.length > 4000) D.raf.splice(0, 2000);
		if (n > D.next_harvest) { D.next_harvest = n + 200; harvest(); }
		cb(t);
	});
	// GPU ms per frame over [now-700, now-200] ms (what had time to come back).
	D.gpu = () => {
		if (!D.qs) return -1;
		const n = performance.now(), a = n - 700, b = n - 200;
		let s = 0; for (const [t, ms] of D.harvested) if (t >= a && t < b) s += ms;
		let f = 0; for (const t of D.raf) if (t >= a && t < b) f++;
		return f ? s / f : -1;
	};
	D.done = () => {
		const n = performance.now(); const xs = D.done_l.filter(([t]) => t > n - 700 && t < n - 100).map((x) => x[1]).sort((x, y) => x - y);
		return xs.length ? xs[xs.length >> 1] : -1;
	};
});
await page.goto(`http://127.0.0.1:${server.address().port}/?${QUERY}`);
const t0 = Date.now();
let ok = false;
while (Date.now() - t0 < TIMEOUT) {
	await new Promise((r) => setTimeout(r, 1000));
	if (await page.evaluate(() => !!window.__dr_done).catch(() => false)) { ok = true; break; }
}
const fail = !adapter || /fallback=true/.test(adapter) || (BROWSER === 'chrome' && !/maxSampledTexturesPerShaderStage=(4[89]|[5-9]\d|\d{3})/.test(adapter)) || (BROWSER === 'chrome' && !/nvidia/i.test(adapter));
writeFileSync(OUT, JSON.stringify({ query: QUERY, browser: BROWSER, adapter, completed: ok, lines }, null, 1));
await browser.close(); server.close();
if (fail) { console.log('ADAPTER CHECK FAILED:', adapter); process.exit(3); }
if (!ok) { console.log('TIMEOUT: page never finished'); process.exit(2); }
console.log('wrote', OUT);
