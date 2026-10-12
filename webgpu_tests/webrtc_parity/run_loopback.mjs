// Runs an exported WebRTC parity fixture in a browser and prints its "RTC|"
// lines so they can be diffed against the native run (see compare.sh).
//
// Serves the export itself with COOP/COEP, which threaded exports require for
// SharedArrayBuffer — so this works for both the nothreads and threads builds
// with no external server.
//
//   RTC_DIR=exports/loopback_threads RTC_TEST=mesh node run_loopback.mjs
import { createServer } from 'node:http';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { chromium } from '/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/node_modules/playwright/index.mjs';

const DIR = resolve(process.env.RTC_DIR || 'exports/loopback');
const TEST = process.env.RTC_TEST || 'loopback';
const TIMEOUT_MS = Number(process.env.RTC_TIMEOUT || 120000);

const MIME = {
	'.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
	'.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json',
	'.worklet.js': 'text/javascript', '.side.wasm': 'application/wasm',
};

if (!existsSync(join(DIR, 'index.html'))) {
	console.error(`[runner] no index.html in ${DIR} — export the fixture project first`);
	process.exit(2);
}

const server = createServer((req, res) => {
	const filePath = join(DIR, decodeURIComponent(req.url.split('?')[0]));
	if (!existsSync(filePath) || !statSync(filePath).isFile()) {
		res.writeHead(404);
		res.end('not found');
		return;
	}
	res.writeHead(200, {
		'Content-Type': MIME[extname(filePath)] || 'application/octet-stream',
		// Required for SharedArrayBuffer, i.e. any threaded export.
		'Cross-Origin-Opener-Policy': 'same-origin',
		'Cross-Origin-Embedder-Policy': 'require-corp',
	});
	res.end(readFileSync(filePath));
});
const port = await new Promise((r) => server.listen(0, '127.0.0.1', () => r(server.address().port)));

const browser = await chromium.launch({
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-gl=angle', '--use-angle=vulkan'],
});
const page = await browser.newPage();
const lines = [];
page.on('console', (m) => {
	const t = m.text();
	if (t.includes('RTC|')) {
		lines.push(t);
		console.log(t.slice(t.indexOf('RTC|')));
	} else if (/ERROR|SCRIPT ERROR|WARNING|Uncaught|abort|RuntimeError/i.test(t)) {
		console.log('[browser-err] ' + t);
	}
});
page.on('pageerror', (e) => console.log('[pageerror] ' + e.message));

const url = `http://127.0.0.1:${port}/index.html?test=${encodeURIComponent(TEST)}`;
await page.goto(url, { waitUntil: 'domcontentloaded' });

// Report cross-origin isolation, since a threaded export silently fails without it.
try {
	const iso = await page.evaluate(() => window.crossOriginIsolated);
	console.log(`[runner] crossOriginIsolated=${iso}`);
} catch { /* page may still be booting */ }

try {
	await page.waitForFunction(() => window.__rtc_done === true, { timeout: TIMEOUT_MS });
	console.log('[runner] fixture signaled completion');
} catch {
	console.log(`[runner] TIMEOUT after ${TIMEOUT_MS}ms — ${lines.length} RTC lines seen`);
}
await browser.close();
server.close();
process.exit(lines.some((l) => l.includes('END')) ? 0 : 1);
