// w52: DisplayServer.window_set_size() on web must resize the root window, viewport and WebGPU surface.
// usage: node resize_test.mjs <export_dir> <out_dir> <label>
// Runs canvas resize policies 0, 1, 2 (rewritten into GODOT_CONFIG by the server) and prints JSON results.
import { createServer } from 'http';
import { readFile, mkdir, writeFile } from 'fs/promises';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium } = require('playwright');

const [DIR, OUT, LABEL] = process.argv.slice(2);
await mkdir(OUT, { recursive: true });
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.pck': 'application/octet-stream' };
const server = createServer(async (req, res) => {
	const u = new URL(req.url, 'http://x');
	const p = path.join(DIR, decodeURIComponent(u.pathname));
	let data;
	try { data = await readFile(p); } catch (e) { res.writeHead(404); res.end(); return; }
	if (p.endsWith('.html') && u.searchParams.has('policy')) {
		const pol = Number(u.searchParams.get('policy'));
		let s = data.toString();
		const n = (s.match(/"canvasResizePolicy":\d/g) || []).length;
		if (n !== 1) { res.writeHead(500); res.end(`canvasResizePolicy matches: ${n}`); return; }
		s = s.replace(/"canvasResizePolicy":\d/, `"canvasResizePolicy":${pol}`);
		// full-size.html forces "Project" over the preset's policy; override that too (at most one such line).
		const forced = (s.match(/GODOT_CONFIG\['canvasResizePolicy'\] = \d;/g) || []).length;
		if (forced > 1) { res.writeHead(500); res.end('forced policy lines: ' + forced); return; }
		s = s.replace(/GODOT_CONFIG\['canvasResizePolicy'\] = \d;/, `GODOT_CONFIG['canvasResizePolicy'] = ${pol};`);
		data = Buffer.from(s);
	}
	res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
	res.end(data);
});
await new Promise((r) => server.listen(8752, '127.0.0.1', r));

const browser = await chromium.launch({
	executablePath: '/usr/bin/google-chrome-stable', headless: false,
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
});
const results = { label: LABEL, adapter: null, policies: {} };
const failures = [];
const check = (cond, msg) => { if (!cond) failures.push(msg); };
try {
	{
		const page = await browser.newPage();
		await page.goto('http://127.0.0.1:8752/index.html?policy=1&probe');
		const a = await page.evaluate(async () => {
			const ad = await navigator.gpu.requestAdapter();
			if (!ad) return null;
			return { vendor: ad.info.vendor, architecture: ad.info.architecture, fallback: !!ad.info.isFallbackAdapter, maxSampled: ad.limits.maxSampledTexturesPerShaderStage };
		});
		await page.close();
		results.adapter = a;
		if (!a || a.vendor !== 'nvidia' || a.architecture !== 'lovelace' || a.fallback || a.maxSampled !== 48) {
			throw new Error('wrong adapter: ' + JSON.stringify(a));
		}
	}
	for (const pol of [0, 1, 2]) {
		const r = { steps: [], warnings: [] };
		results.policies[pol] = r;
		const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
		page.on('console', (m) => {
			const t = m.text();
			if (m.type() === 'error' || m.type() === 'warning') r.warnings.push(`${m.type()}: ${t.slice(0, 300)}`);
		});
		await page.goto(`http://127.0.0.1:8752/index.html?policy=${pol}`);
		await page.waitForFunction(() => (window.__log || []).some((e) => e.tag === 'ready'), null, { timeout: 90000 });
		await page.waitForTimeout(1000);
		const snap = async (name, settle = 600) => {
			await page.waitForTimeout(settle);
			await page.evaluate(() => { window.__cmd = 'report'; });
			await page.waitForFunction(() => window.__cmd === '', null, { timeout: 10000 });
			await page.waitForTimeout(100);
			const s = await page.evaluate(() => {
				const c = document.getElementById('canvas');
				const log = window.__log;
				return { godot: log[log.length - 1], canvas: [c.width, c.height], css: [c.style.width, c.style.height], dpr: devicePixelRatio, inner: [innerWidth, innerHeight], policy: GODOT_CONFIG.canvasResizePolicy };
			});
			s.name = name;
			r.steps.push(s);
			await page.screenshot({ path: path.join(OUT, `${LABEL}_p${pol}_${name}.png`) });
			return s;
		};
		const cmd = async (c) => {
			await page.evaluate((c) => { window.__cmd = c; }, c);
			await page.waitForFunction(() => window.__cmd === '', null, { timeout: 10000 });
		};
		const s0 = await snap('start');
		check(s0.policy === pol, `policy in effect ${s0.policy}, wanted ${pol}`);
		// Click the 800x600 button (canvas pixels 20..220 x 20..70).
		const box = await page.locator('#canvas').boundingBox();
		const sx = box.width / s0.canvas[0], sy = box.height / s0.canvas[1];
		await page.mouse.click(box.x + 120 * sx, box.y + 45 * sy);
		const s1 = await snap('button_800x600');
		await cmd(`set ${s0.godot.window[0]} ${s0.godot.window[1]}`);
		const s2 = await snap('set_back');
		await cmd('wset 700 500');
		const s3 = await snap('window_size_700x500');
		await cmd(`wset ${s0.godot.window[0]} ${s0.godot.window[1]}`);
		const s4 = await snap('window_size_back');
		await page.setViewportSize({ width: 1000, height: 700 });
		const s5 = await snap('browser_1000x700', 1000);
		await page.setViewportSize({ width: 1280, height: 800 });
		const s6 = await snap('browser_1280x800', 1000);
		const eq = (a, b) => a[0] === b[0] && a[1] === b[1];
		const consistent = (s, tag) => {
			const g = s.godot;
			check(eq(g.window, s.canvas) && eq(g.visible_rect, s.canvas) && eq(g.ds, s.canvas) && eq(g.corner_label, s.canvas),
				`p${pol} ${tag}: window/viewport/corner ${JSON.stringify(g)} vs canvas ${s.canvas}`);
		};
		for (const s of [s0, s1, s2, s5, s6]) consistent(s, s.name);
		if (pol === 1) {
			check(eq(s1.canvas, [800, 600]) && s1.godot.signals === s0.godot.signals + 1, `p1 button: canvas ${s1.canvas} signals ${s0.godot.signals}->${s1.godot.signals}`);
			check(eq(s2.canvas, s0.canvas) && s2.godot.signals === s1.godot.signals + 1, `p1 set back: canvas ${s2.canvas} signals ${s1.godot.signals}->${s2.godot.signals}`);
			check(eq(s3.canvas, [700, 500]), `p1 Window.size: canvas ${s3.canvas}`);
			consistent(s3, 'wset'); consistent(s4, 'wset back');
			check(eq(s5.canvas, s0.canvas) && eq(s6.canvas, s0.canvas), `p1 browser resize changed canvas`);
		} else if (pol === 0) {
			check(eq(s1.canvas, s0.canvas) && s1.godot.signals === s0.godot.signals, `p0 window_set_size changed something`);
			check(eq(s5.canvas, s0.canvas) && eq(s6.canvas, s0.canvas), `p0 browser resize changed canvas`);
		} else {
			check(eq(s1.canvas, s0.canvas), `p2 window_set_size changed canvas`);
			check(eq(s5.canvas, [Math.floor(1000 * s5.dpr), Math.floor(700 * s5.dpr)]) && s5.godot.signals > s4.godot.signals, `p2 browser resize 1000x700: ${s5.canvas}`);
			check(eq(s6.canvas, [Math.floor(1280 * s6.dpr), Math.floor(800 * s6.dpr)]), `p2 browser resize back: ${s6.canvas}`);
		}
		check(!r.warnings.some((w) => /differs from configured/.test(w)), `p${pol}: surface-size warning`);
		await page.close();
	}
} catch (e) {
	failures.push('exception: ' + e.message);
}
results.failures = failures;
await writeFile(path.join(OUT, `${LABEL}.json`), JSON.stringify(results, null, 1));
for (const [pol, r] of Object.entries(results.policies)) {
	console.log(`policy ${pol}:`);
	for (const s of r.steps) console.log(`  ${s.name.padEnd(22)} canvas ${s.canvas} window ${s.godot.window} visible ${s.godot.visible_rect} corner ${s.godot.corner_label} signals ${s.godot.signals}`);
	console.log(`  warnings/errors: ${r.warnings.length}${r.warnings.length ? '\n    ' + r.warnings.slice(0, 8).join('\n    ') : ''}`);
}
console.log(failures.length ? 'FAIL\n  ' + failures.join('\n  ') : 'PASS');
await browser.close();
server.close();
process.exit(failures.length ? 1 : 0);
