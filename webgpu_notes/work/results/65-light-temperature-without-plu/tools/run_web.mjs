// run_web.mjs <export dir> <out dir>: serves an lt.gd web export, asserts the adapter, screenshots the canvas at each
// "[LT] READY <tag>" into <out dir>/<tag>.png. Headed system Chrome on the real GPU (work README launch rule).
import { createServer } from 'http';
import { readFileSync, existsSync, writeFileSync, mkdirSync } from 'fs';
import { join, extname } from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/package.json');
const pw = require('playwright');
const [dir, out] = process.argv.slice(2);
mkdirSync(out, { recursive: true });
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = createServer((req, res) => {
	const p = join(dir, decodeURIComponent(req.url.split('?')[0]).replace(/^\/$/, '/index.html'));
	if (!existsSync(p)) { res.writeHead(404); res.end(); return; }
	res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
	res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
	res.setHeader('Content-Type', types[extname(p)] || 'application/octet-stream');
	res.end(readFileSync(p));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const browser = await pw.chromium.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false,
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'], ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] });
let rc = 0;
try {
	const page = await browser.newPage({ viewport: { width: 320, height: 240 } });
	await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
	const a = await page.evaluate(async () => {
		const ad = await navigator.gpu.requestAdapter();
		return ad ? { vendor: ad.info.vendor, arch: ad.info.architecture, fallback: ad.info.isFallbackAdapter, tex: ad.limits.maxSampledTexturesPerShaderStage } : null;
	});
	console.log('adapter', JSON.stringify(a));
	if (!a || !/nvidia/i.test(a.vendor) || !/lovelace/i.test(a.arch) || a.fallback || a.tex < 48) throw new Error('wrong adapter');
	const lines = [];
	const shots = [];
	let done;
	const finished = new Promise((r) => (done = r));
	page.on('console', (m) => {
		const t = m.text();
		if (t.startsWith('[LT] READY ')) shots.push(page.locator('canvas').screenshot({ path: join(out, t.split(' ')[2] + '.png') }));
		if (/\[LT\]|ERROR/.test(t)) { console.log(t); lines.push(t); }
		if (t.includes('[LT] DONE')) done();
	});
	// The page may already have printed before the handler; reload so every line is seen.
	await page.reload();
	await Promise.race([finished, new Promise((_, rej) => setTimeout(() => rej(new Error('timeout: no [LT] DONE in 240 s')), 240000))]);
	await Promise.all(shots);
	writeFileSync(join(out, 'log.txt'), lines.join('\n') + '\n');
	console.log('shots', shots.length);
} catch (e) {
	console.error('FAIL', e.message);
	rc = 1;
}
await browser.close();
server.close();
process.exit(rc);
