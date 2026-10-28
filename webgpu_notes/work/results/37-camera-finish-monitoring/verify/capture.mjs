// capture.mjs <base-url> <outdir> <name=query>... : system Chrome, headed, real GPU; canvas screenshots per case.
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/package.json');
const { chromium } = require('playwright');
const [base, outdir, ...cases] = process.argv.slice(2);
const browser = await chromium.launch({
	executablePath: '/usr/bin/google-chrome-stable', headless: false,
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1 });
page.on('console', (m) => { if (/error|EDITOR_HINT/i.test(m.text())) console.log('console:', m.text().slice(0, 300)); });
await page.goto(base + 'index.html');
const info = await page.evaluate(async () => {
	const a = await navigator.gpu.requestAdapter();
	return { vendor: a.info.vendor, architecture: a.info.architecture, maxSampled: a.limits.maxSampledTexturesPerShaderStage };
});
console.log('adapter', JSON.stringify(info));
for (const c of cases) {
	const [name, query] = c.split('=', 2).length > 1 ? [c.slice(0, c.indexOf('=')), c.slice(c.indexOf('=') + 1)] : [c, ''];
	await page.goto(base + 'index.html?' + query);
	await page.waitForFunction(() => { const s = document.getElementById('status'); return !s || getComputedStyle(s).visibility === 'hidden'; }, null, { timeout: 180000 });
	await page.waitForFunction(() => typeof window.__shot === 'string', null, { timeout: 60000 });
	const b64 = await page.evaluate(() => window.__shot);
	(await import('fs')).writeFileSync(`${outdir}/${name}.png`, Buffer.from(b64, 'base64'));
	console.log('saved', name);
}
await browser.close();
