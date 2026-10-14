/**
 * Main-thread CPU profile of an export's startup, up to the engine's first presented frame
 * (TASKS.md 2.4): which engine functions the callMain() stall is spent in.
 *
 * Needs a template built with `debug_symbols=yes` (keeps the wasm name section; the wasm is
 * ~600 MB, so load-phase numbers from it are NOT representative -- use profile_phases.mjs on a
 * normal build for those). The editor must be built with `webgpu=yes` or the export is unbaked.
 * Sampling perturbs queue calls badly past the first frame, which is why it stops there.
 *
 *   node cpu_profile.mjs --dir <export-dir> [--wait 30000] [--top 60]
 */
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, mkdtempSync } from 'fs';
import { join, extname, resolve } from 'path';
import { tmpdir } from 'os';
import { fileURLToPath } from 'url';
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir')), WAIT = +arg('wait', 7000), TOP = +arg('top', 50);
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png' };
const server = createServer((req, res) => {
	const p = decodeURIComponent(req.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { res.writeHead(404); res.end(); return; }
	res.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Content-Length': statSync(f).size }); res.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const { chromium } = await import(join(fileURLToPath(new URL('.', import.meta.url)), '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
const ctx = await chromium.launchPersistentContext(mkdtempSync(join(tmpdir(), 'cp-')), { headless: false, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--gpu-no-context-lost', '--disable-gpu-shader-disk-cache'], viewport: { width: 1280, height: 720 } });
const page = await ctx.newPage(); const cdp = await ctx.newCDPSession(page);
await cdp.send('Profiler.enable'); await cdp.send('Profiler.setSamplingInterval', { interval: 250 }); await cdp.send('Profiler.start');
await page.addInitScript(() => { window.__ff = false; window.addEventListener('godot-webgpu-first-frame', () => { window.__ff = true; }); });
await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
{ const t0 = Date.now(); while (Date.now() - t0 < WAIT && !(await page.evaluate(() => window.__ff))) await new Promise((r) => setTimeout(r, 20)); }
const { profile } = await cdp.send('Profiler.stop'); await ctx.close(); server.close();
const nodes = new Map(profile.nodes.map((n) => [n.id, n])), parent = new Map();
for (const n of profile.nodes) for (const c of n.children || []) parent.set(c, n.id);
const self = new Map(), incl = new Map(); let total = 0;
const nm = (id) => nodes.get(id).callFrame.functionName || '(anon)';
profile.samples.forEach((id, i) => { const dt = (profile.timeDeltas[i] || 0) / 1000; total += dt; self.set(nm(id), (self.get(nm(id)) || 0) + dt);
	const seen = new Set(); for (let c = id; c !== undefined; c = parent.get(c)) { const k = nm(c); if (seen.has(k)) continue; seen.add(k); incl.set(k, (incl.get(k) || 0) + dt); } });
console.log(`total ${total.toFixed(0)} ms, idle ${(self.get('(idle)') || 0).toFixed(0)}`);
const show = (m, t) => { console.log('\n' + t); [...m].filter(([k]) => k !== '(idle)').sort((a, b) => b[1] - a[1]).slice(0, TOP).forEach(([k, v]) => console.log(`${v.toFixed(0).padStart(6)} ms  ${k.slice(0, 140)}`)); };
show(self, 'SELF'); show(incl, 'INCLUSIVE');
