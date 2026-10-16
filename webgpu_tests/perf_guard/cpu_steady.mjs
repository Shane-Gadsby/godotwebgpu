// Steady-state main-thread CPU profile of a WebGPU export (main thread only: see api_profile.mjs about thread_model=Separate) (needs a template built with debug_symbols=yes so wasm
// functions are named). Same launch as api_profile.mjs. Prints self time by function, and inclusive time for the
// functions that match --incl (substring list), as % of the sampled window.
//   node cpu_steady.mjs --dir <export> [--uncapped] [--measure 5000] [--top 40] [--incl a,b,c] [--json out.json]
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir')), SETTLE = +arg('settle', 15000), MEASURE = +arg('measure', 5000), TOP = +arg('top', 40);
const INCL = arg('incl', '').split(',').filter(Boolean);
const UNCAPPED = process.argv.includes('--uncapped');
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
// COOP/COEP on every response: threaded exports need cross-origin isolation, and same-origin exports are unaffected.
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Content-Length': statSync(f).size, 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const { chromium } = await import(join(HERE, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
const browser = await chromium.launch({
	headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist', ...(UNCAPPED ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : [])],
});
const page = await browser.newPage();
const cdp = await page.context().newCDPSession(page);
await page.addInitScript(() => { window.__frames = 0; const raf = window.requestAnimationFrame.bind(window); window.requestAnimationFrame = (cb) => raf((t) => { window.__frames++; cb(t); }); });
await page.goto(`http://127.0.0.1:${server.address().port}/`);
await new Promise((r) => setTimeout(r, SETTLE));
await cdp.send('Profiler.enable'); await cdp.send('Profiler.setSamplingInterval', { interval: 100 });
const f0 = await page.evaluate(() => window.__frames);
await cdp.send('Profiler.start'); await new Promise((r) => setTimeout(r, MEASURE));
const { profile } = await cdp.send('Profiler.stop');
const f1 = await page.evaluate(() => window.__frames); const frames = Math.max(f1 - f0, 1);
const nodes = new Map(profile.nodes.map((n) => [n.id, n]));
const parent = new Map(); for (const n of profile.nodes) for (const c of n.children || []) parent.set(c, n.id);
const self = new Map(), incl = new Map(); let total = 0;
profile.samples.forEach((id, i) => {
	const dt = (profile.timeDeltas[i] || 0) / 1000; total += dt;
	const nm = (n) => nodes.get(n).callFrame.functionName || '(anon)';
	self.set(nm(id), (self.get(nm(id)) || 0) + dt);
	const seen = new Set();
	for (let n = id; n !== undefined; n = parent.get(n)) { const k = nm(n); if (!seen.has(k)) { seen.add(k); incl.set(k, (incl.get(k) || 0) + dt); } }
});
const perFrame = (ms) => (ms / frames).toFixed(3);
console.log(`frames ${frames}, sampled ${total.toFixed(0)} ms (${perFrame(total)} ms/frame)`);
console.log('--- self time, ms/frame');
for (const [k, v] of [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, TOP)) console.log(perFrame(v).padStart(8) + '  ' + k.slice(0, 110));
if (INCL.length) {
	console.log('--- inclusive time, ms/frame');
	for (const [k, v] of [...incl.entries()].filter(([k]) => INCL.some((s) => k.includes(s))).sort((a, b) => b[1] - a[1]).slice(0, 40)) console.log(perFrame(v).padStart(8) + '  ' + k.slice(0, 110));
}
const out = arg('json', null); if (out) writeFileSync(out, JSON.stringify({ frames, total, self: [...self], incl: [...incl] }));
await browser.close(); server.close();
