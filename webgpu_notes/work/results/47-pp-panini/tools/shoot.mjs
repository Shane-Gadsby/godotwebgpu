// Serves task 47's web export and screenshots the canvas per case. System Chrome, headed, Vulkan WebGPU; the adapter
// is asserted first (nvidia/lovelace, not fallback, 48 sampled textures) and the run fails otherwise.
// node shoot.mjs <export dir> <out dir> case...
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { chromium } from '/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright/index.mjs';
const [dir, outDir, ...cases] = process.argv.slice(2);
fs.mkdirSync(outDir, { recursive: true });
const types = { wasm: 'application/wasm', js: 'text/javascript', html: 'text/html', pck: 'application/octet-stream', png: 'image/png' };
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
await page.goto(`http://127.0.0.1:${port}/index.html?case=none`);
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
for (const name of cases) {
	try {
		await page.goto(`http://127.0.0.1:${port}/index.html?case=${name}&layout=target`);
		await page.waitForFunction(() => document.title === 'ready', null, { timeout: 180000 });
		await page.waitForFunction(() => {
			const s = document.getElementById('status');
			return !s || getComputedStyle(s).display === 'none' || getComputedStyle(s).visibility === 'hidden';
		}, null, { timeout: 180000 });
		await page.waitForTimeout(1500);
		await page.locator('canvas').screenshot({ path: path.join(outDir, `${name}.png`) });
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
