// Start-screen test: parent on localhost:8701, game on 127.0.0.1:8702 (served at ~MBPS MB/s).
// usage: node start_test.mjs <game_dir> <parent_dir> <out_dir> <scenario: ready|early|fullscreen-first> [MBPS]
import { createServer } from 'http';
import { readFile } from 'fs/promises';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium } = require('playwright');

const [GAME, PARENT, OUT, SCENARIO, MBPS = '0'] = process.argv.slice(2);
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.pck': 'application/octet-stream' };
const requests = {};
function serve(dir, host, port, mbps) {
	return new Promise((resolve) => {
		const s = createServer(async (req, res) => {
			const name = decodeURIComponent(req.url.split('?')[0]);
			requests[`${port}${name}`] = (requests[`${port}${name}`] || 0) + 1;
			const p = path.join(dir, name);
			let data;
			try { data = await readFile(p); } catch (e) { res.writeHead(404); res.end(); return; }
			res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream', 'Content-Length': data.length });
			if (!mbps || data.length < 1e6) { res.end(data); return; }
			// Throttled: 1 MB pieces at mbps MB/s.
			let off = 0;
			const tick = () => {
				if (off >= data.length) { res.end(); return; }
				res.write(data.subarray(off, off + 1e6));
				off += 1e6;
				setTimeout(tick, 1000 / mbps);
			};
			tick();
		});
		s.listen(port, host, () => resolve(s));
	});
}
const s1 = await serve(PARENT, 'localhost', 8701, 0);
const s2 = await serve(GAME, '127.0.0.1', 8702, Number(MBPS));

const browser = await chromium.launch({
	headless: false,
	executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
});
const page = await browser.newPage({ viewport: null });
page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') console.log('console:', m.type(), m.text().slice(0, 600)); });
const t0 = Date.now();
const T = () => `${((Date.now() - t0) / 1000).toFixed(1)}s`;
await page.goto('http://localhost:8701/index.html');
const frame = () => page.frames().find((f) => f.url().startsWith('http://127.0.0.1:8702'));
while (!frame()) await page.waitForTimeout(100);
await frame().waitForSelector('#status-start', { state: 'visible' });

const state = async (label) => {
	const s = await frame().evaluate(() => ({
		engineRunning: !!(engine && engine.rtenv),
		canvasCss: document.getElementById('canvas').style.width,
		canvas: [document.getElementById('canvas').width, document.getElementById('canvas').height],
		statusShown: !!document.getElementById('status'),
		preload: (document.getElementById('status-preload') || {}).textContent,
	}));
	const fs = await page.evaluate(() => document.fullscreenElement ? document.fullscreenElement.id : null);
	console.log(T(), label, JSON.stringify({ ...s, parentFullscreen: fs }));
};

await page.waitForTimeout(1500);
await state('start screen');
await page.screenshot({ path: `${OUT}/1_start.png` });

if (SCENARIO === 'ready') {
	while ((await frame().evaluate(() => document.getElementById('status-preload').textContent)) !== 'Ready') await page.waitForTimeout(200);
	await state('preload done');
	await page.waitForTimeout(5000);
	await state('5 s later, still not started');
} else if (SCENARIO === 'fullscreen-first') {
	await page.click('#sim-fullscreen');
	await page.waitForTimeout(1500);
	await state('fullscreen on start screen');
	await page.screenshot({ path: `${OUT}/2_start_fullscreen.png` });
}

const tStart = Date.now();
await frame().click('#status-start');
console.log(T(), 'clicked Start');
await page.waitForTimeout(500);
await state('0.5 s after Start');
await page.screenshot({ path: `${OUT}/3_loading.png` });
await frame().waitForFunction(() => !document.getElementById('status'), null, { timeout: 200000, polling: 100 });
console.log(T(), `game up ${((Date.now() - tStart) / 1000).toFixed(1)} s after Start`);
await page.waitForTimeout(3000);
await state('running');
await page.screenshot({ path: `${OUT}/4_running.png` });
console.log('requests to game server:', JSON.stringify(Object.fromEntries(Object.entries(requests).filter(([k]) => k.startsWith('8702')))));

await browser.close();
s1.close(); s2.close();
