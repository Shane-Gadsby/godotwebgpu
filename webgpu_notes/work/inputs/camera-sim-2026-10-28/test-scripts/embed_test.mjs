// Cross-origin iframe fullscreen test: parent on localhost:8701, game on 127.0.0.1:8702.
// usage: node embed_test.mjs <game_dir> <parent_dir> <out_dir> [exitClickX exitClickY]
import { createServer } from 'http';
import { readFile } from 'fs/promises';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium } = require('playwright');

const [GAME, PARENT, OUT, EX, EY] = process.argv.slice(2);
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.pck': 'application/octet-stream' };
function serve(dir, host, port) {
	return new Promise((resolve) => {
		const s = createServer(async (req, res) => {
			const p = path.join(dir, decodeURIComponent(req.url.split('?')[0]));
			try {
				const data = await readFile(p);
				res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] || 'application/octet-stream' });
				res.end(data);
			} catch (e) { res.writeHead(404); res.end(); }
		});
		s.listen(port, host, () => resolve(s));
	});
}
const s1 = await serve(PARENT, 'localhost', 8701);
const s2 = await serve(GAME, '127.0.0.1', 8702);

const browser = await chromium.launch({
	headless: false,
	executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
});
const page = await browser.newPage({ viewport: null });
page.on('console', (m) => { if (m.type() === 'error' || /fullscreen|SCRIPT|ERROR/i.test(m.text())) console.log('console:', m.type(), m.text()); });
await page.goto('http://localhost:8701/index.html');
const frame = () => page.frames().find((f) => f.url().startsWith('http://127.0.0.1:8702'));

const state = async (label) => {
	const f = frame();
	const s = await f.evaluate(() => {
		const c = document.getElementById('canvas');
		return { inner: [innerWidth, innerHeight], dpr: devicePixelRatio, canvas: [c.width, c.height], css: [c.style.width, c.style.height], transform: c.style.transform };
	});
	const fs = await page.evaluate(() => document.fullscreenElement ? document.fullscreenElement.id : null);
	console.log(label, JSON.stringify({ ...s, parentFullscreen: fs }));
};

await page.waitForTimeout(25000);
await state('loaded');
await page.screenshot({ path: `${OUT}/1_windowed.png` });

await page.click('#sim-fullscreen');
await page.waitForTimeout(4000);
await state('after parent button');
await page.screenshot({ path: `${OUT}/2_fullscreen.png` });

if (EX) {
	// EX/EY are in the 1656x968 layout; the fullscreen iframe sits at 0,0 and the
	// UI is scaled to fit it (canvas_items, keep aspect), centred.
	const [w, h] = await frame().evaluate(() => [innerWidth, innerHeight]);
	const k = Math.min(w / 1656, h / 968);
	const x = (w - 1656 * k) / 2 + Number(EX) * k, y = (h - 968 * k) / 2 + Number(EY) * k;
	await page.mouse.click(x, y);
	console.log('clicked in-game button at', x, y);
} else {
	await page.evaluate(() => document.exitFullscreen());
	console.log('parent exitFullscreen()');
}
await page.waitForTimeout(4000);
await state('after exit');
await page.screenshot({ path: `${OUT}/3_exited.png` });

await browser.close();
s1.close(); s2.close();
