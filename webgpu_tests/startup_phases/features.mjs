// Reports the WebGPU adapter features each driven browser exposes, which is what
// decides safely shippable texture-compression formats for a web export.
import { createServer } from 'http';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
import { mkdtempSync } from 'fs';
import { tmpdir } from 'os';
const __dirname = dirname(fileURLToPath(import.meta.url));
const pw = await import(join(__dirname, '..', 'scene_smoketest', 'node_modules', 'playwright', 'index.mjs'));
// WebGPU needs a secure context, which a data: URL is not -- serve over localhost.
const server = createServer((req, res) => { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<title>features</title>'); });
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/`;
for (const name of ['chromium', 'firefox']) {
	const ctx = await pw[name].launchPersistentContext(mkdtempSync(join(tmpdir(), `feat-${name}-`)), {
		headless: false,
		args: name === 'chromium' ? ['--enable-unsafe-webgpu', '--enable-features=Vulkan'] : [],
		firefoxUserPrefs: name === 'firefox' ? { 'dom.webgpu.enabled': true, 'gfx.webrender.all': true, 'gfx.webgpu.ignore-blocklist': true } : undefined,
	});
	const page = await ctx.newPage();
	await page.goto(url);
	const out = await page.evaluate(async () => {
		if (!navigator.gpu) { return { error: 'navigator.gpu missing' }; }
		const a = await navigator.gpu.requestAdapter();
		if (!a) { return { error: 'no adapter' }; }
		const info = a.info || {};
		return { features: [...a.features].sort(), vendor: info.vendor, architecture: info.architecture, description: info.description };
	});
	console.log(`\n=== ${name} ===`);
	if (out.error) { console.log('  ', out.error); } else {
		console.log(`   adapter: ${out.vendor || '?'} / ${out.architecture || '?'} ${out.description || ''}`);
		console.log(`   features (${out.features.length}): ${out.features.join(', ')}`);
		for (const f of ['texture-compression-bc', 'texture-compression-etc2', 'texture-compression-astc', 'subgroups']) {
			console.log(`     ${out.features.includes(f) ? 'YES' : 'no '}  ${f}`);
		}
	}
	await ctx.close();
}
server.close();
