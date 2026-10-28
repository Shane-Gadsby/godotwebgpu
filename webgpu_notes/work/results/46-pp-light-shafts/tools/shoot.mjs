// node shoot.mjs <export dir> <out dir> name=query ... : serves the export, screenshots the canvas per case.
// System Chrome, headed, Vulkan WebGPU; fails unless the adapter is nvidia/lovelace, not fallback, 48 textures.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
const { chromium } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright/index.mjs');
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
await page.goto(`http://127.0.0.1:${port}/index.png`);
const info = await page.evaluate(async () => {
	const a = await navigator.gpu.requestAdapter();
	return { vendor: a.info.vendor, arch: a.info.architecture, fallback: a.info.isFallbackAdapter, tex: a.limits.maxSampledTexturesPerShaderStage };
});
console.log('adapter', JSON.stringify(info));
if (info.vendor !== 'nvidia' || info.arch !== 'lovelace' || info.fallback || info.tex !== 48) {
	console.log('FAILED adapter check');
	await browser.close(); server.close(); process.exit(1);
}
let failed = 0;
for (const c of cases) {
	const [name, q] = c.split(/=(.*)/s);
	try {
		logs.length = 0;
		await page.goto(`http://127.0.0.1:${port}/index.html?${q}`);
		await page.waitForFunction(() => document.title === 'ready', null, { timeout: 180000 });
		await page.waitForFunction(() => {
			const s = document.getElementById('status');
			return !s || getComputedStyle(s).display === 'none' || getComputedStyle(s).visibility === 'hidden';
		}, null, { timeout: 180000 });
		await page.waitForTimeout(1000);
		await page.locator('canvas').screenshot({ path: path.join(outDir, `${name}.png`) });
		const bad = logs.filter((l) => /^\[error\]|USER ERROR|ERROR:/.test(l));
		console.log('shot', name, logs.filter((l) => /READY|GPU_MS/.test(l)).join(' '), bad.length ? `ERRORS: ${bad.slice(0, 5).join(' | ')}` : '');
		if (bad.length) failed++;
	} catch (e) {
		failed++;
		console.log('FAILED', name, e.message.split('\n')[0], logs.slice(-5).join(' | '));
	}
}
await browser.close();
server.close();
process.exit(failed ? 1 : 0);
