// Serves the task 33 web export and screenshots the canvas per case. System Chrome, headed, Vulkan WebGPU.
// node shoot.mjs <export dir> <out dir>; cases are in CASES below (name -> URL query).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu-33/webgpu_tests/forward_plus/');
const { chromium } = require('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright');
const [dir, outDir] = process.argv.slice(2);
fs.mkdirSync(outDir, { recursive: true });
const CASES = {
	vig_f24_n1_4: 'scene=flat&bg=0.5&attrs=physical&vignetting_enabled=true&frustum_focal_length=24&exposure_aperture=1.4&lens_max_aperture=1.4',
	vig_prac: 'scene=flat&bg=0.5&attrs=practical&vignetting_enabled=true&vignetting_strength=1.5&vignetting_falloff=3.0',
	dots_off: 'scene=dots&attrs=practical',
	dots_barrel: 'scene=dots&attrs=practical&distortion_enabled=true&distortion_k1=-0.1&distortion_k2=0.02',
	ca: 'scene=dots&attrs=practical&chromatic_aberration_enabled=true&chromatic_aberration_strength=5',
	edge_off: 'scene=edge&attrs=physical&sensor_format=3&exposure_aperture=64',
	edge_f64: 'scene=edge&attrs=physical&sensor_format=3&exposure_aperture=64&diffraction_enabled=true',
	default_phys: 'scene=default&attrs=physical',
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
for (const [name, q] of Object.entries(CASES)) {
	try {
		await page.goto(`http://127.0.0.1:${port}/index.html?${q}`);
		await page.waitForFunction(() => document.title === 'ready', null, { timeout: 120000 });
		await page.waitForFunction(() => {
			const s = document.getElementById('status');
			return !s || getComputedStyle(s).display === 'none' || getComputedStyle(s).visibility === 'hidden';
		}, null, { timeout: 180000 });
		await page.waitForTimeout(1500);
		await page.locator('canvas').screenshot({ path: path.join(outDir, `${name}.web.png`) });
		console.log('shot', name);
	} catch (e) {
		failed++;
		console.log('FAILED', name, e.message.split('\n')[0]);
	}
}
console.log(logs.filter((l) => /error|warn|Forward|Mobile|WebGPU/i.test(l)).slice(0, 30).join('\n'));
await browser.close();
server.close();
process.exit(failed ? 1 : 0);
