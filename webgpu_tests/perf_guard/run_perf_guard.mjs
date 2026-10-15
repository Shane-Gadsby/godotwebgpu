// Per-frame WebGPU API budgets (TASKS.md 2.16 gap 4, found by 2.17).
//
// Frame time is adapter- and display-dependent, so it cannot be asserted in CI; what regressed in the post-fx scene
// (an encoder + submit per render pass, and whole-slice uploads of a persistent buffer) was the *number of WebGPU
// calls and bytes per frame*, which is the same on any adapter. This wraps every method of the GPU* classes in a
// page, lets a scene settle, counts for a few seconds and asserts per-frame ceilings.
//
//   node run_perf_guard.mjs [--scene benchmark_postfx] [--dir <exports dir>] [--print]
//
// Needs the scene smoketest's exports (run_scenes.mjs --export-only). Exit 2 + "SKIP --" if they are missing.
// CI=1 runs headless on the software adapter (Forward Mobile); otherwise system Chrome on the real GPU with the
// project's standard flags. Budgets are ceilings with headroom over what was measured, not targets.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname, resolve, dirname } from 'path';
import { fileURLToPath } from 'url';

const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const EXPORTS = resolve(arg('dir', join(HERE, '..', 'scene_smoketest', 'exports')));
const ONLY = arg('scene', null);
const PRINT = process.argv.includes('--print');
const SETTLE_MS = 12000, MEASURE_MS = CI_RUN() ? 15000 : 5000;
// A software adapter renders a handful of frames a second; the per-frame counts are still exact, so only require a few.
const MIN_FRAMES = CI_RUN() ? 3 : 30;
function CI_RUN() { return !!process.env.CI; }

// Per-frame ceilings, ~3x what was measured on 2026-10-15 (real GPU, Forward+). encoders/submits: 2 (80 in the post-fx
// scene before the per-pass encoder split was removed). writeKB: 65-350 KB (10 MB in the post-fx scene, and a flat
// 2 MB in every scene from the canvas instance buffer, before the persistent-buffer range flush).
const BUDGETS = {
	benchmark_postfx: { encoders: 8, submits: 8, writeCalls: 260, writeKB: 1024 },
	benchmark_shadows: { encoders: 8, submits: 8, writeCalls: 100, writeKB: 768 },
	benchmark_instances: { encoders: 8, submits: 8, writeCalls: 60, writeKB: 768 },
	benchmark_pbr: { encoders: 8, submits: 8, writeCalls: 60, writeKB: 512 },
};

const scenes = Object.keys(BUDGETS).filter((s) => !ONLY || s === ONLY);
const missing = scenes.filter((s) => !existsSync(join(EXPORTS, s, 'index.html')));
if (missing.length === scenes.length) {
	console.log(`SKIP -- no exports in ${EXPORTS}; run scene_smoketest/run_scenes.mjs --export-only first`);
	process.exit(2);
}

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
function serve(dir) {
	return new Promise((res) => {
		const server = createServer((req, rsp) => {
			const p = decodeURIComponent(req.url.split('?')[0]);
			const f = join(dir, p === '/' ? 'index.html' : p);
			if (!existsSync(f) || !statSync(f).isFile()) { rsp.writeHead(404); rsp.end(); return; }
			rsp.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
			rsp.end(readFileSync(f));
		});
		server.listen(0, '127.0.0.1', () => res(server));
	});
}

const { chromium } = await import(join(HERE, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
const CI = !!process.env.CI;
const launchOpts = CI
	? { headless: true, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer', '--use-angle=swiftshader', '--enable-gpu'] }
	: {
		headless: false,
		executablePath: '/usr/bin/google-chrome-stable',
		ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	};

const INSTRUMENT = () => {
	window.__stat = { calls: {}, writeBytes: 0, frames: 0, on: false };
	const wrap = (cls) => {
		const proto = window[cls] && window[cls].prototype;
		if (!proto) return;
		for (const k of Object.getOwnPropertyNames(proto)) {
			const d = Object.getOwnPropertyDescriptor(proto, k);
			if (k === 'constructor' || !d || typeof d.value !== 'function') continue;
			const f = d.value;
			proto[k] = function (...a) {
				if (window.__stat.on) {
					const key = cls + '.' + k;
					window.__stat.calls[key] = (window.__stat.calls[key] || 0) + 1;
					if (key === 'GPUQueue.writeBuffer') {
						const data = a[2];
						const n = a[4] !== undefined ? a[4] * (data.BYTES_PER_ELEMENT || 1) : (data.byteLength !== undefined ? data.byteLength : data.length);
						window.__stat.writeBytes += n - (a[3] || 0) * (data.BYTES_PER_ELEMENT || 1) * (a[4] === undefined ? 1 : 0);
					}
				}
				return f.apply(this, a);
			};
		}
	};
	for (const c of ['GPUDevice', 'GPUQueue', 'GPUCommandEncoder']) wrap(c);
	const raf = window.requestAnimationFrame.bind(window);
	window.requestAnimationFrame = (cb) => raf((t) => { if (window.__stat.on) window.__stat.frames++; cb(t); });
};

let failures = 0;
const browser = await chromium.launch(launchOpts);
for (const scene of scenes) {
	if (missing.includes(scene)) { console.log(`  SKIP  ${scene} (no export)`); continue; }
	const server = await serve(join(EXPORTS, scene));
	const page = await browser.newPage();
	await page.addInitScript(INSTRUMENT);
	await page.goto(`http://127.0.0.1:${server.address().port}/`);
	await new Promise((r) => setTimeout(r, SETTLE_MS));
	await page.evaluate(() => { window.__stat.calls = {}; window.__stat.writeBytes = 0; window.__stat.frames = 0; window.__stat.on = true; });
	await new Promise((r) => setTimeout(r, MEASURE_MS));
	const st = await page.evaluate(() => window.__stat);
	await page.close(); server.close();

	const f = Math.max(st.frames, 1);
	const per = (k) => (st.calls[k] || 0) / f;
	const got = {
		encoders: per('GPUDevice.createCommandEncoder'),
		submits: per('GPUQueue.submit'),
		writeCalls: per('GPUQueue.writeBuffer'),
		writeKB: st.writeBytes / f / 1024,
	};
	// PERF_GUARD_SCALE=0.1 shrinks every ceiling: a way to prove the guard can go red (it must, on any scene).
	const scale = parseFloat(process.env.PERF_GUARD_SCALE || '1');
	const b = Object.fromEntries(Object.entries(BUDGETS[scene]).map(([k, v]) => [k, v * scale]));
	const bad = Object.keys(b).filter((k) => got[k] > b[k]);
	const line = Object.keys(b).map((k) => `${k}=${got[k].toFixed(1)}/${b[k]}`).join('  ');
	if (st.frames < MIN_FRAMES) {
		console.log(`  FAIL  ${scene}: only ${st.frames} frames in ${MEASURE_MS} ms -- the scene did not run`);
		failures++;
	} else if (bad.length) {
		console.log(`  FAIL  ${scene}  ${line}   over: ${bad.join(', ')}`);
		failures++;
	} else {
		console.log(`  PASS  ${scene}  ${line}  (${st.frames} frames)`);
	}
	if (PRINT) console.log('        calls/frame: ' + Object.entries(st.calls).sort((x, y) => y[1] - x[1]).slice(0, 6).map(([k, v]) => `${k}=${(v / f).toFixed(1)}`).join(' '));
}
await browser.close();
console.log(failures === 0 ? '\nRESULT: PASS' : `\nRESULT: FAIL (${failures})`);
process.exit(failures === 0 ? 0 : 1);
