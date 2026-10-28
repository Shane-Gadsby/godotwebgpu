// Serves the task 45 web export and screenshots the canvas per case. System Chrome, headed, Vulkan WebGPU.
// node shoot.mjs <export dir> <out dir>; cases in CASES (name -> URL query); each waits for document.title "ready".
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/');
const { chromium } = require('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright');
const [dir, outDir] = process.argv.slice(2);
fs.mkdirSync(outDir, { recursive: true });
// Task 45: the grading chart per case on the web (system Chrome headed, Vulkan WebGPU); asserts the adapter first.
const CASES = [];
for (const c of 'off user3d_only user1d_only neutral filter hue_shift vibrance vibrance_neg vibrance_min lift gamma gain smh mixer split curve_master curve_rgb hue_vs_hue hue_vs_sat sat_vs_sat lum_vs_sat all all_user3d all_user1d all_adjust'.split(' ')) CASES.push([c, 'lattice']);
for (const c of 'off neutral all hue_shift vibrance curve_master'.split(' ')) CASES.push([c, 'off']);
const types = { wasm: 'application/wasm', js: 'text/javascript', html: 'text/html', pck: 'application/octet-stream', png: 'image/png', bin: 'application/octet-stream' };
const server = http.createServer((req, res) => {
	const f = path.join(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
	if (!fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end(); }
	res.writeHead(200, { 'Content-Type': types[f.split('.').pop()] || 'application/octet-stream' });
	fs.createReadStream(f).pipe(res);
}).listen(0, '127.0.0.1');
await new Promise((r) => server.on('listening', r));
const port = server.address().port;
const browser = await chromium.launch({
	headless: false, executablePath: '/usr/bin/google-chrome-stable',
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
});
const page = await browser.newPage({ viewport: { width: 1080, height: 1080 }, deviceScaleFactor: 1 });
const logs = [];
page.on('console', (m) => logs.push(`[${m.type()}] ${m.text()}`));
await page.goto(`http://127.0.0.1:${port}/index.html`);
const info = await page.evaluate(async () => {
	const a = await navigator.gpu.requestAdapter();
	return a ? { vendor: a.info.vendor, arch: a.info.architecture, fallback: a.info.isFallbackAdapter, tex: a.limits.maxSampledTexturesPerShaderStage } : null;
});
console.log('adapter', JSON.stringify(info));
if (!info || info.vendor !== 'nvidia' || info.arch !== 'lovelace' || info.fallback || info.tex < 48) {
	console.log('ADAPTER BAD');
	await browser.close();
	server.close();
	process.exit(1);
}
let failed = 0;
for (const [c, chart] of CASES) {
	const name = `${c}_${chart}`;
	try {
		await page.goto(`http://127.0.0.1:${port}/index.html?case=${c}&chart=${chart}`);
		await page.waitForFunction(() => document.title === 'ready', null, { timeout: 180000 });
		await page.waitForFunction(() => {
			const s = document.getElementById('status');
			return !s || getComputedStyle(s).display === 'none' || getComputedStyle(s).visibility === 'hidden';
		}, null, { timeout: 180000 });
		await page.waitForTimeout(300);
		await page.locator('canvas').screenshot({ path: path.join(outDir, `${name}.png`) });
		console.log('shot', name);
	} catch (e) {
		failed++;
		console.log('FAILED', name, e.message.split('\n')[0]);
	}
}
console.log(logs.filter((l) => /error|warn|Forward|Mobile|WebGPU/i.test(l)).slice(0, 40).join('\n'));
await browser.close();
server.close();
process.exit(failed ? 1 : 0);
