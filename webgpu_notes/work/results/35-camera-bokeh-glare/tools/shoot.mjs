// Serves the task 35 web export (adapted from task 33's) and screenshots the canvas per case. System Chrome, headed, Vulkan WebGPU.
// node shoot.mjs <export dir> <out dir>; cases are in CASES below (name -> URL query).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu-35/webgpu_tests/forward_plus/');
const { chromium } = require('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright');
const [dir, outDir] = process.argv.slice(2);
fs.mkdirSync(outDir, { recursive: true });
const CASES = {
	"def_new_forward_plus_physical": "scene=default&attrs=physical",
	"blades_forward_plus_7_20": "scene=points&points=0,0&point_size=0.08&point_energy=20&cam.frustum_focal_length=50.0&cam.frustum_focus_distance=1.0&cam.exposure_aperture=1.4&cam.aperture_enabled=true&cam.aperture_blade_count=7&cam.aperture_rotation=20.0",
	"catseye_forward_plus_f1.4": "scene=points&points=0,0;0.45,0;0.9,0;0,0.85;0.6,0.6;-0.9,-0.85&point_size=0.08&point_energy=20&cam.frustum_focal_length=50.0&cam.frustum_focus_distance=1.0&cam.exposure_aperture=1.4&cam.lens_max_aperture=1.4&cam.cats_eye_enabled=true&cam.aperture_enabled=true&cam.aperture_blade_count=0",
	"tilt_forward_plus_-6.0": "scene=ground&cam_height=1.5&cam_pitch=-20&cam.frustum_focal_length=50.0&cam.frustum_focus_distance=3.0&cam.exposure_aperture=1.4&cam.tilt_shift_enabled=true&cam.tilt_shift_tilt=-6.0",
	"star_forward_plus_6_f16": "scene=points&points=0,0&point_size=0.02&point_energy=200&cam.frustum_focal_length=50.0&cam.frustum_focus_distance=20.0&cam.starburst_enabled=true&cam.starburst_strength=400.0&cam.aperture_blade_count=6&cam.exposure_aperture=16.0",
	"star_forward_plus_7_f16": "scene=points&points=0,0&point_size=0.02&point_energy=200&cam.frustum_focal_length=50.0&cam.frustum_focus_distance=20.0&cam.starburst_enabled=true&cam.starburst_strength=3000.0&cam.exposure_aperture=16.0&cam.aperture_blade_count=7",
	"flare_forward_plus_4": "scene=points&points=0.5,0.4&point_size=0.08&point_energy=200&attrs=none&env.lens_flare_enabled=true&env.lens_flare_intensity=40.0&env.lens_flare_threshold=0.0&env.lens_flare_halo_intensity=0.0&env.lens_flare_chromatic_aberration=0.0&env.lens_flare_ghost_count=4",
	"halation_forward_plus_r4.0": "scene=points&points=0,0&point_size=0.03&point_energy=40&cam.frustum_focal_length=50.0&cam.frustum_focus_distance=20.0&cam.exposure_aperture=16.0&cam.halation_enabled=true&cam.halation_strength=12.0&cam.halation_radius=4.0",
	"halves_forward_plus_veil5": "scene=halves&card_value=0.5&attrs=none&env.veiling_glare_enabled=true&env.veiling_glare_strength=5.0",
	"halves_forward_plus_diff1": "scene=halves&card_value=0.5&attrs=none&env.diffusion_filter_enabled=true&env.diffusion_filter_strength=1.0",
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
