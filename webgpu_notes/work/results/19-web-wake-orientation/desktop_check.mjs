// Task 19 desktop check: the probe export (wake_proj/, preset Default) in headed Chrome (system, Vulkan flags) and
// Firefox (Playwright's), real GPU. Records the probe (has_feature answers), what the browser itself supports, every
// console line, and the state after a click (the project toggles fullscreen on release). Take the `gpu` lease first.
//
//   PLAYWRIGHT=<path to playwright/index.mjs> node desktop_check.mjs <export dir> <out.json>
import { createServer } from 'http';
import { existsSync, readFileSync, statSync, writeFileSync } from 'fs';
import { extname, join, resolve } from 'path';

const DIR = resolve(process.argv[2]);
const OUT = resolve(process.argv[3] || 'desktop_check.json');
const pw = await import(process.env.PLAYWRIGHT || 'playwright');
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.json': 'application/json' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]);
	const f = join(DIR, p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	const body = readFileSync(f);
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp', 'Cache-Control': 'no-store' });
	r.end(body);
});
await new Promise((res) => server.listen(8097, '127.0.0.1', res));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const browsers = {
	chrome: () => pw.chromium.launch({ channel: 'chrome', headless: false, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'] }),
	firefox: () => pw.firefox.launch({ headless: false, firefoxUserPrefs: { 'dom.webgpu.enabled': true, 'gfx.webgpu.ignore-blocklist': true } }),
};
const report = {};
for (const [name, launch] of Object.entries(browsers)) {
	const r = { console: [] };
	report[name] = r;
	let browser;
	try {
		browser = await launch();
		const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
		page.on('console', (m) => r.console.push({ type: m.type(), text: m.text() }));
		page.on('pageerror', (e) => r.console.push({ type: 'pageerror', text: String(e) }));
		await page.goto('http://localhost:8097/index.html');
		const caps = async () => page.evaluate(async () => {
			const a = navigator.gpu && await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
			const info = a && (a.info || (a.requestAdapterInfo && await a.requestAdapterInfo()));
			return { version: navigator.userAgent, wakeLockApi: !!navigator.wakeLock, orientationLockApi: !!(screen.orientation && screen.orientation.lock),
				coarse: matchMedia('(any-pointer: coarse)').matches, adapter: info ? `${info.vendor} ${info.architecture}` : null };
		});
		r.browser = await caps();
		for (let i = 0; i < 90 && !r.probe; i++) {
			await sleep(1000);
			r.probe = await page.evaluate(() => window.__wake_orient_probe || null);
		}
		await sleep(2000);
		await page.mouse.click(640, 360);
		await sleep(2500);
		r.after_click = await page.evaluate(() => ({ fullscreen: !!document.fullscreenElement, type: screen.orientation && screen.orientation.type }));
		await page.mouse.click(640, 360);
		await sleep(2500);
		r.after_second_click = await page.evaluate(() => ({ fullscreen: !!document.fullscreenElement }));
		r.expected = { keep_screen_on_feature: r.browser.wakeLockApi, orientation_feature: r.browser.orientationLockApi && r.browser.coarse };
		r.has_feature_matches = !!r.probe && r.probe.keep_screen_on_feature === r.expected.keep_screen_on_feature && r.probe.orientation_feature === r.expected.orientation_feature;
		r.errors_or_warnings = r.console.filter((c) => /error|warn/.test(c.type));
	} catch (e) {
		r.fatal = String(e);
	}
	if (browser) await browser.close();
	console.log(name, JSON.stringify({ browser: r.browser, probe: r.probe, after_click: r.after_click, matches: r.has_feature_matches, errors_or_warnings: r.errors_or_warnings, fatal: r.fatal }));
}
writeFileSync(OUT, JSON.stringify(report, null, 1));
server.close();
