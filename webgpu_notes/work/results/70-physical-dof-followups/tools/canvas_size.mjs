// Prints the canvas's backing size for a viewport size (node canvas_size.mjs <export dir> <WxH>). System Chrome, headed.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/');
const { chromium } = require('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright');
const [dir, size] = process.argv.slice(2);
const [w, h] = size.split('x').map(Number);
const types = { wasm: 'application/wasm', js: 'text/javascript', html: 'text/html' };
const server = http.createServer((req, res) => {
	const f = path.join(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
	if (!fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end(); }
	res.writeHead(200, { 'Content-Type': types[f.split('.').pop()] || 'application/octet-stream' });
	fs.createReadStream(f).pipe(res);
}).listen(0, '127.0.0.1');
await new Promise((r) => server.on('listening', r));
const browser = await chromium.launch({
	headless: false, executablePath: '/usr/bin/google-chrome-stable',
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
});
const page = await browser.newPage({ viewport: { width: w, height: h } });
await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
const info = await page.evaluate(async () => {
	const a = await navigator.gpu.requestAdapter();
	return { vendor: a.info.vendor, arch: a.info.architecture, fallback: a.info.isFallbackAdapter, tex: a.limits.maxSampledTexturesPerShaderStage };
});
if (info.vendor !== 'nvidia' || info.arch !== 'lovelace' || info.fallback || info.tex < 48) { console.log('FAILED adapter', JSON.stringify(info)); process.exit(2); }
await page.waitForFunction(() => document.title === 'ready', null, { timeout: 120000 });
console.log(size, await page.evaluate(() => { const c = document.querySelector('canvas'); return `${c.width}x${c.height} dpr ${devicePixelRatio}`; }));
await browser.close(); server.close();
