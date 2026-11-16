// Web check: Start, histogram, zebras + false colour on, Download Image and Download Screenshot.
// usage: node dl2_test.mjs <game_dir> <out_dir>
import { createServer } from 'http';
import { readFile } from 'fs/promises';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium } = require('playwright');
const [GAME, OUT] = process.argv.slice(2);
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png' };
const server = createServer(async (req, res) => {
	const p = path.join(GAME, decodeURIComponent(req.url.split('?')[0]));
	try { const d = await readFile(p); res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' }); res.end(d); }
	catch { res.writeHead(404); res.end(); }
});
await new Promise((r) => server.listen(8704, '127.0.0.1', r));
const browser = await chromium.launch({ headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist', '--window-size=1656,1100'] });
const page = await browser.newPage({ viewport: { width: 1656, height: 968 }, acceptDownloads: true });
let errors = 0;
page.on('console', (m) => {
	const t = m.text();
	if (m.type() === 'error' || m.type() === 'warning' || /WebGPU|Forward|readback|histogram/i.test(t)) console.log(`[${m.type()}]`, t.slice(0, 300));
	if (m.type() === 'error' && !t.includes('Skin bind') && !t.includes('skeleton_3d')) errors++;
});
page.on('pageerror', (e) => { console.log('[pageerror]', e.message); errors++; });
await page.goto('http://127.0.0.1:8704/index.html');
await page.click('#status-start');
const t0 = Date.now();
await page.waitForFunction(() => !document.getElementById('status'), null, { timeout: 200000, polling: 200 });
console.log('game up after Start', (Date.now() - t0) / 1000, 's');
await page.waitForTimeout(4000);
await page.screenshot({ path: `${OUT}/web_start.png` });
await page.mouse.click(1010, 911); // Zebra Lines
await page.mouse.click(1010, 877); // False Colour
await page.waitForTimeout(1500);
await page.screenshot({ path: `${OUT}/web_overlays.png` });
for (const [name, x, y] of [['photo', 1364, 771], ['screenshot', 1364, 797]]) {
	const dl = page.waitForEvent('download', { timeout: 20000 });
	const t1 = Date.now();
	await page.mouse.click(x, y);
	const d = await dl;
	await d.saveAs(`${OUT}/web_${name}.png`);
	console.log('downloaded', name, d.suggestedFilename(), (Date.now() - t1), 'ms');
	await page.waitForTimeout(800);
}
// two photos at once
const both = [page.waitForEvent('download', { timeout: 20000 })];
await page.mouse.click(1364, 771);
await page.waitForTimeout(30);
await page.mouse.click(1364, 771);
const d1 = await both[0];
console.log('double-click photo ok', d1.suggestedFilename());
await page.waitForTimeout(2000);
console.log('ERRORS (excluding Skin bind):', errors);
await browser.close();
server.close();
