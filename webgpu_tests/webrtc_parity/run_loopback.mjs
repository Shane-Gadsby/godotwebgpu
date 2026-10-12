// Runs the exported in-process WebRTC loopback in a browser and prints the
// "RTC|" lines so they can be diffed against the native run.
import { chromium } from '/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/node_modules/playwright/index.mjs';

const URL = process.env.RTC_URL || 'http://localhost:8099/index.html';
const TIMEOUT_MS = Number(process.env.RTC_TIMEOUT || 90000);

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
	} else if (/ERROR|SCRIPT ERROR|Uncaught/i.test(t)) {
		console.log('[browser-err] ' + t);
	}
});
page.on('pageerror', (e) => console.log('[pageerror] ' + e.message));

await page.goto(URL, { waitUntil: 'domcontentloaded' });
try {
	await page.waitForFunction(() => window.__rtc_done === true, { timeout: TIMEOUT_MS });
	console.log('\n[runner] fixture signalled completion');
} catch {
	console.log(`\n[runner] TIMEOUT after ${TIMEOUT_MS}ms — ${lines.length} RTC lines seen`);
}
await browser.close();
process.exit(lines.some((l) => l.includes('END')) ? 0 : 1);
