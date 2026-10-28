// Serves the task 36 web export and screenshots the canvas per case. System Chrome, headed, Vulkan WebGPU.
// node shoot.mjs <export dir> <out dir>; cases in CASES (name -> URL query); each waits for document.title "ready".
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/');
const { chromium } = require('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright');
const [dir, outDir] = process.argv.slice(2);
fs.mkdirSync(outDir, { recursive: true });
const AE = 'auto_exposure_enabled=true&auto_exposure_max_sensitivity=1000000&auto_exposure_speed=20';
const CASES = {
	meter_ref: 'scene=meter&frames=90',
	meter_avg: `scene=meter&frames=90&${AE}`,
	meter_center: `scene=meter&frames=90&${AE}&auto_exposure_metering_mode=1`,
	meter_spot: `scene=meter&frames=90&${AE}&auto_exposure_metering_mode=2`,
	meter_spotwin: `scene=meter&frames=90&${AE}&auto_exposure_metering_mode=2&auto_exposure_metering_spot_point=0.75,0.3&auto_exposure_metering_spot_size=1.0`,
	meter_hl99: `scene=meter&frames=90&${AE}&auto_exposure_metering_mode=3`,
	meter_hl90: `scene=meter&frames=90&${AE}&auto_exposure_metering_mode=3&auto_exposure_metering_highlight_percentile=90`,
	awb_ref: 'scene=awb&exposure=0.5&frames=12',
	awb_on: 'scene=awb&exposure=0.5&frames=500&white_balance_auto=true&white_balance_auto_speed=3',
	awb_clamp: 'scene=awb&exposure=0.5&frames=500&white_balance_auto=true&white_balance_auto_speed=3&white_balance_auto_min_temperature=4000',
	awb_bias: 'scene=awb&exposure=0.5&frames=500&white_balance_auto=true&white_balance_auto_speed=3&white_balance_temperature=5000&white_balance_tint=20',
	default_prac: 'scene=default&attrs=practical&frames=12',
	default_ae: 'scene=default&attrs=practical&frames=12&auto_exposure_enabled=true',
};
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
const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1 });
const logs = [];
page.on('console', (m) => logs.push(`[${m.type()}] ${m.text()}`));
await page.goto(`http://127.0.0.1:${port}/index.html`);
const info = await page.evaluate(async () => {
	const a = await navigator.gpu.requestAdapter();
	return { vendor: a.info.vendor, arch: a.info.architecture, fallback: a.info.isFallbackAdapter, tex: a.limits.maxSampledTexturesPerShaderStage };
});
console.log('adapter', JSON.stringify(info));
let failed = 0;
const only = process.env.ONLY ? process.env.ONLY.split(',') : null;
for (const [name, q] of Object.entries(CASES)) {
	if (only && !only.includes(name)) continue;
	try {
		await page.goto(`http://127.0.0.1:${port}/index.html?res=1280x720&${q}`);
		await page.waitForFunction(() => document.title === 'ready', null, { timeout: 180000 });
		await page.waitForFunction(() => {
			const s = document.getElementById('status');
			return !s || getComputedStyle(s).display === 'none' || getComputedStyle(s).visibility === 'hidden';
		}, null, { timeout: 180000 });
		await page.locator('canvas').screenshot({ path: path.join(outDir, `${name}.web.png`) });
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
