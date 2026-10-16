// Where does a WebGPU export's main-thread frame time go? (TASKS.md 2.17)
//
//   node api_profile.mjs --dir <export dir> [--uncapped] [--top 14] [--settle 12000] [--measure 6000]
//
// Reports main-thread busy % (CDP Performance metrics: ~100% = CPU-bound), frames/s, and for every GPU* class method the
// calls per frame and milliseconds per frame spent inside the call, so a CPU-bound scene shows which WebGPU calls it is
// paying for. Standard launch flags (system Chrome); --uncapped adds Chrome's frame-rate-limit/vsync flags so fps is not
// the display rate.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const TOP = +arg('top', 14), SETTLE = +arg('settle', 12000), MEASURE = +arg('measure', 6000);
const UNCAPPED = process.argv.includes('--uncapped');
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
// COOP/COEP on every response: threaded exports need cross-origin isolation, and same-origin exports are unaffected.
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const { chromium } = await import(join(HERE, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
const browser = await chromium.launch({
	headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist', ...(UNCAPPED ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : [])],
});
const SIZE = arg('size', ''); // e.g. 640x360: a GPU-bound frame speeds up when the canvas shrinks, a CPU-bound one does not
const page = await browser.newPage(SIZE ? { viewport: { width: +SIZE.split('x')[0], height: +SIZE.split('x')[1] } } : {});
await page.addInitScript(() => {
	window.__t = {}; window.__on = false; window.__frames = 0; window.__wb = {};
	const wrap = (cls) => {
		const proto = window[cls] && window[cls].prototype; if (!proto) return;
		for (const k of Object.getOwnPropertyNames(proto)) {
			const d = Object.getOwnPropertyDescriptor(proto, k); if (k === 'constructor' || !d || typeof d.value !== 'function') continue;
			const f = d.value, key = cls + '.' + k;
			proto[k] = function (...a) {
				if (!window.__on) return f.apply(this, a);
				const t = performance.now();
				try { return f.apply(this, a); } finally { const e = window.__t[key] || (window.__t[key] = { n: 0, ms: 0 }); e.n++; e.ms += performance.now() - t; }
			};
		}
	};
	{ // writeBuffer detail: which buffer/size/offset the expensive writes are (the queue method is wrapped generically above too)
		const ow = GPUQueue.prototype.writeBuffer;
		GPUQueue.prototype.writeBuffer = function (buf, off, data, ...r) {
			if (!window.__on) return ow.call(this, buf, off, data, ...r);
			const n = r[1] !== undefined ? r[1] * (data.BYTES_PER_ELEMENT || 1) : data.byteLength;
			const t = performance.now(); const x = ow.call(this, buf, off, data, ...r); const dt = performance.now() - t;
			const k = `buf ${buf.size} usage ${buf.usage} write ${n} B`; const e = window.__wb[k] || (window.__wb[k] = { n: 0, ms: 0 }); e.n++; e.ms += dt; return x;
		};
	}
	for (const c of ['GPUDevice', 'GPUQueue', 'GPUCommandEncoder', 'GPURenderPassEncoder', 'GPUComputePassEncoder', 'GPUBuffer', 'GPUTexture', 'GPUCanvasContext']) wrap(c);
	const raf = window.requestAnimationFrame.bind(window);
	window.requestAnimationFrame = (cb) => raf((t) => { if (window.__on) window.__frames++; cb(t); });
});
const cdp = await page.context().newCDPSession(page); await cdp.send('Performance.enable');
await page.goto(`http://127.0.0.1:${server.address().port}/`);
await new Promise((r) => setTimeout(r, SETTLE));
const metrics = async () => Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map((m) => [m.name, m.value]));
await page.evaluate(() => { window.__t = {}; window.__frames = 0; window.__on = true; });
const m0 = await metrics(); await new Promise((r) => setTimeout(r, MEASURE)); const m1 = await metrics();
const r = await page.evaluate(() => ({ t: window.__t, frames: window.__frames, stats: window.godotWebGPUFrameStats || null, wb: window.__wb }));
const wall = m1.Timestamp - m0.Timestamp, f = Math.max(r.frames, 1);
let tot = 0; const rows = Object.entries(r.t).map(([k, v]) => { tot += v.ms; return [k, v.n / f, v.ms / f]; }).sort((a, b) => b[2] - a[2]);
console.log(`fps ${(f / wall).toFixed(0)}   frame ${(1000 * wall / f).toFixed(2)} ms   main-thread busy ${(100 * (m1.TaskDuration - m0.TaskDuration) / wall).toFixed(0)}%   inside WebGPU calls ${(tot / f).toFixed(2)} ms/frame`);
if (r.stats) console.log('engine frame stats: ' + JSON.stringify(r.stats));
for (const [k, n, ms] of rows.slice(0, TOP)) console.log(k.padEnd(40) + n.toFixed(1).padStart(8) + ' calls/f' + ms.toFixed(3).padStart(9) + ' ms/f');
if (process.argv.includes('--writes')) { console.log('--- writeBuffer by buffer / size'); for (const [k, v] of Object.entries(r.wb).sort((a, b) => b[1].ms - a[1].ms).slice(0, 8)) console.log(k.padEnd(52) + (v.n / f).toFixed(1).padStart(7) + ' /f' + (v.ms / f).toFixed(3).padStart(9) + ' ms/f  (' + (1000 * v.ms / v.n).toFixed(0) + ' us each)'); }
await browser.close(); server.close();
