// Task 74: load the capture bench export in the mandated Chrome launch, assert the adapter, collect the [cap] lines.
//   node run_web.mjs --dir <export> --query "up=fsr2&s0=1.0&s1=0.6" --out x.json [--size 1920x1080] [--timeout 240]
import { createServer } from 'http';
import { readFileSync, existsSync, statSync, writeFileSync } from 'fs';
import { join, extname, resolve } from 'path';
const REPO = '/mnt/109313D2109313D2/godot-editors/godotwebgpu';
const { chromium } = await import(join(REPO, 'webgpu_tests/scene_smoketest/node_modules/playwright/index.mjs'));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = resolve(arg('dir'));
const QUERY = arg('query', '');
const OUT = arg('out', 'out.json');
const [W, H] = arg('size', '1920x1080').split('x').map(Number);
const TIMEOUT = +arg('timeout', 240) * 1000;
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]); const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); r.end(readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const browser = await chromium.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false,
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] });
let code = 0;
try {
	const page = await browser.newPage({ viewport: { width: W, height: H } });
	const lines = [];
	const adapters = [];
	page.on('console', (m) => {
		const t = m.text();
		if (t.startsWith('[cap] ')) { lines.push(JSON.parse(t.slice(6))); console.log(t.slice(0, 400)); }
		else if (t.startsWith('[capjs] ')) { adapters.push(t); console.log(t); }
		else if (/error|warning|Mobile|Forward/i.test(t)) console.log('  page:', t.slice(0, 300));
	});
	await page.addInitScript(() => {
		const ad = GPU.prototype.requestAdapter;
		GPU.prototype.requestAdapter = async function (...a) {
			const ada = await ad.apply(this, a);
			if (ada) {
				const i = ada.info || {};
				console.log(`[capjs] adapter ${i.vendor}/${i.architecture} fallback=${i.isFallbackAdapter} maxSampledTexturesPerShaderStage=${ada.limits.maxSampledTexturesPerShaderStage}`);
			} else console.log('[capjs] adapter null');
			return ada;
		};
	});
	await page.goto(`http://127.0.0.1:${server.address().port}/index.html?${QUERY}`);
	await page.waitForFunction(() => window.__cap_done === true, null, { timeout: TIMEOUT, polling: 500 });
	const a = adapters.find((x) => x.includes('adapter')) || '';
	if (!/nvidia\/lovelace fallback=false maxSampledTexturesPerShaderStage=48/.test(a)) {
		console.error('ADAPTER ASSERT FAILED:', a);
		code = 2;
	}
	writeFileSync(OUT, JSON.stringify({ query: QUERY, adapter: a, lines }, null, 1));
} catch (e) {
	console.error('FAILED:', e.message);
	code = 1;
} finally {
	await browser.close();
	server.close();
}
process.exit(code);
