// web_capture.mjs <export_dir> <out_prefix> <query>... : serves the export, opens it in system Chrome (real GPU,
// the required flags), asserts the adapter, waits for the loading overlay to hide plus ~3 s, saves the canvas.
import { chromium } from '/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright/index.mjs';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const [dir, outPrefix, ...queries] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.bin': 'application/octet-stream' };
const server = http.createServer((req, res) => {
	const p = path.join(dir, decodeURIComponent(req.url.split('?')[0]).replace(/^\/$/, '/index.html'));
	if (!fs.existsSync(p)) { res.writeHead(404); res.end(); return; }
	res.writeHead(200, { 'Content-Type': types[path.extname(p)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
	fs.createReadStream(p).pipe(res);
});
await new Promise((r) => server.listen(0, r));
const port = server.address().port;

const b = await chromium.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false,
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] });
let fail = 0;
try {
	const ctx = await b.newContext({ viewport: { width: 960, height: 540 }, deviceScaleFactor: 1 });
	const page = await ctx.newPage();
	await page.goto(`http://127.0.0.1:${port}/index.html?probe`);
	const a = await page.evaluate(async () => { const ad = await navigator.gpu.requestAdapter(); return ad ? { v: ad.info.vendor, arch: ad.info.architecture, fb: ad.info.isFallbackAdapter, t: ad.limits.maxSampledTexturesPerShaderStage } : null; });
	console.log('adapter', JSON.stringify(a));
	if (!a || a.v !== 'nvidia' || a.arch !== 'lovelace' || a.fb || a.t < 48) { console.log('ADAPTER BAD'); process.exit(1); }
	for (const q of queries) {
		const errors = [];
		page.removeAllListeners('console');
		page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') errors.push(m.text()); });
		await page.goto(`http://127.0.0.1:${port}/index.html?${q}`);
		// The shell removes the #status overlay from the DOM once the engine runs (setStatusMode('hidden')).
		await page.waitForFunction(() => !document.getElementById('status'), null, { timeout: 120000 });
		await page.waitForTimeout(3000);
		const name = `${outPrefix}_${q.replace(/[^a-z0-9]+/gi, '_')}.png`;
		await page.locator('canvas').screenshot({ path: name });
		console.log('saved', name, 'console errors/warnings:', errors.length);
		for (const e of errors.slice(0, 8)) console.log('  ', e.slice(0, 300));
	}
} catch (e) { console.log('FAIL', e.message); fail = 1; }
await b.close();
server.close();
process.exit(fail);
