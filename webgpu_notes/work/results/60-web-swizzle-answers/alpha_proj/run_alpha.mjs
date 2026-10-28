// Loads a task-60 scratch export in system Chrome (Vulkan flags) or Firefox (WebGPU prefs), asserts the adapter,
// waits for "[ALPHA] done", screenshots the canvas and prints the console.
// usage: node run_alpha.mjs <export dir> <out.png> [chrome|firefox]
import { createRequire } from 'module';
import fs from 'fs';
import http from 'http';
import path from 'path';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/scene_smoketest/package.json');
const { chromium, firefox } = require('playwright');
const [dir, out, which = 'chrome'] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.bin': 'application/octet-stream' };
const server = http.createServer((req, res) => {
	const p = path.join(dir, req.url === '/' ? 'index.html' : decodeURIComponent(req.url.split('?')[0]));
	if (!fs.existsSync(p)) { res.writeHead(404); return res.end(); }
	res.setHeader('content-type', types[path.extname(p)] || 'application/octet-stream');
	res.end(fs.readFileSync(p));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
let browser;
if (which === 'firefox') {
	const { FIREFOX_WEBGPU_PREFS } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/scene_smoketest/firefox_prefs.mjs');
	browser = await firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS });
} else {
	browser = await chromium.launch({
		executablePath: '/usr/bin/google-chrome-stable', headless: false,
		args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
		ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	});
}
const page = await browser.newPage({ viewport: { width: 1024, height: 512 } });
const lines = [];
page.on('console', (m) => lines.push(`[${m.type()}] ${m.text()}`));
await page.goto(`http://127.0.0.1:${server.address().port}/`);
const ad = await page.evaluate(async () => {
	const a = await navigator.gpu.requestAdapter();
	return a && { vendor: a.info.vendor, architecture: a.info.architecture, fallback: a.info.isFallbackAdapter, maxSampled: a.limits.maxSampledTexturesPerShaderStage };
});
console.log('adapter', JSON.stringify(ad));
if (which === 'firefox') {
	if (!ad) { console.log('BAD ADAPTER'); process.exit(1); }
} else if (!ad || ad.fallback || ad.vendor !== 'nvidia' || ad.architecture !== 'lovelace' || ad.maxSampled < 48) {
	console.log('BAD ADAPTER'); process.exit(1);
}
const deadline = Date.now() + 90000;
while (!lines.some((l) => l.includes('[ALPHA] done')) && Date.now() < deadline) await new Promise((r) => setTimeout(r, 200));
if (!lines.some((l) => l.includes('[ALPHA] done'))) { console.log('TIMEOUT waiting for [ALPHA] done'); for (const l of lines) console.log(l.slice(0, 300)); process.exit(1); }
await page.waitForFunction(() => { const s = document.getElementById('status'); return !s || getComputedStyle(s).display === 'none' || s.style.visibility === 'hidden'; }, null, { timeout: 30000 });
await new Promise((r) => setTimeout(r, 500));
await page.locator('canvas').screenshot({ path: out });
for (const l of lines) console.log(l.split('\n')[0].slice(0, 400));
await browser.close();
server.close();
