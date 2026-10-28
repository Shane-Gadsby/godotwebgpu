// Serves the task 36 web export and screenshots the canvas per case. System Chrome, headed, Vulkan WebGPU.
// node shoot40.mjs <export dir> <out dir> <cases.json>; each waits for document.title "ready".
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/');
const { chromium } = require('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright');
const [dir, outDir] = process.argv.slice(2, 4);
fs.mkdirSync(outDir, { recursive: true });
// Cases: a JSON file {name: query} as the third argument.
const CASES = JSON.parse(fs.readFileSync(process.argv[4], 'utf8'));
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
if (info.vendor !== 'nvidia' || info.arch !== 'lovelace' || info.fallback || info.tex < 48) { console.log('FAILED adapter check'); await browser.close(); server.close(); process.exit(2); }
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
