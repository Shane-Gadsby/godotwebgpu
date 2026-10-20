// Runs an exported WebRTC parity fixture in a browser and prints its "RTC|"
// lines so they can be diffed against the native run (see compare.sh).
//
// Serves the export itself with COOP/COEP, which threaded exports require for
// SharedArrayBuffer — so this works for the nothreads, threads and dlink builds
// with no external server.
//
//   RTC_DIR=exports/loopback_threads RTC_TEST=mesh node run_loopback.mjs
//   RTC_BROWSER=firefox node run_loopback.mjs
//
// RTC_BROWSER is chromium (default), firefox, or safari. Safari is macOS-only
// and driven by AppleScript rather than Playwright, exactly as
// scene_smoketest/run_scenes.mjs drives it, and needs Safari's
// Develop → "Allow JavaScript from Apple Events".
//
// The fixture's output is read from `window.__rtc_lines`, which
// tests/rtc_test_base.gd appends to alongside every console.log. The console is
// only used for the error channel: polling the array gives the same lines in
// every browser including Safari (where the console cannot be read at all), and
// each line is printed exactly once, which the diff depends on.
import { createServer } from 'node:http';
import { execSync } from 'node:child_process';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { extname, join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const DIR = resolve(process.env.RTC_DIR || 'exports/loopback');
const TEST = process.env.RTC_TEST || 'loopback';
const BROWSER = (process.env.RTC_BROWSER || 'chromium').toLowerCase();
const TIMEOUT_MS = Number(process.env.RTC_TIMEOUT || 120000);
const POLL_MS = 250;

if (!['chromium', 'firefox', 'safari'].includes(BROWSER)) {
	console.error(`[runner] unknown RTC_BROWSER=${BROWSER} (chromium|firefox|safari)`);
	process.exit(2);
}

// Playwright from this tier's own node_modules, falling back to the other
// tiers' copies so a checkout that installed it elsewhere still works. An
// absolute path was hardcoded here and could only ever work on one machine.
async function loadPlaywright() {
	const here = dirname(fileURLToPath(import.meta.url));
	const candidates = [
		join(here, 'node_modules/playwright/index.mjs'),
		join(here, '../screenshot_comparison/node_modules/playwright/index.mjs'),
		join(here, '../resource_lifecycle/node_modules/playwright/index.mjs'),
	];
	for (const c of candidates) {
		if (existsSync(c)) {
			return import(pathToFileURL(c).href);
		}
	}
	return import('playwright');
}

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
const url = `http://127.0.0.1:${port}/index.html?test=${encodeURIComponent(TEST)}`;

// Prints whatever the fixture has appended since the last call. `read` returns
// { lines, done } or null when the page cannot be reached yet.
let printed = 0;
const seen = [];
function emit(lines) {
	for (const line of lines.slice(printed)) {
		seen.push(line);
		console.log(line);
	}
	printed = lines.length;
}

async function pollUntilDone(read) {
	const deadline = Date.now() + TIMEOUT_MS;
	while (Date.now() < deadline) {
		const state = await read();
		if (state) {
			emit(state.lines);
			if (state.done) {
				console.log('[runner] fixture signaled completion');
				return true;
			}
		}
		await new Promise((r) => setTimeout(r, POLL_MS));
	}
	console.log(`[runner] TIMEOUT after ${TIMEOUT_MS}ms — ${seen.length} RTC lines seen`);
	return false;
}

async function runPlaywright(name) {
	const pw = await loadPlaywright();
	// Firefox has no swiftshader-style software WebGPU on Linux, so on a
	// GPU-less runner the engine has no adapter and never boots. That is a
	// missing adapter, not a WebRTC failure — run it headful locally and let CI
	// report it as a skip.
	const isCI = !!process.env.CI;
	const browser = name === 'firefox'
		? await pw.firefox.launch({
			headless: isCI,
			firefoxUserPrefs: { 'dom.webgpu.enabled': true, 'gfx.webgpu.force-enabled': true },
		})
		: await pw.chromium.launch({
			// The user's own launcher (see scene_smoketest/run_scenes.mjs): system Chrome, headed.
			// --use-angle=vulkan / --use-vulkan=native / --no-sandbox make it report "WebGL2 missing".
			// `headless` has to be explicit: Playwright defaults to headless, where this Chrome gets
			// only the swiftshader fallback adapter and cannot present a WebGPU canvas at all (the GPU
			// process logs "Could not find SharedImageBackingFactory ... WebgpuSwapChainTexture" and the
			// device is lost at the first present). TASKS.md §3 trap 43.
			headless: isCI,
			...(isCI ? {} : { executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] }),
			args: isCI
				? ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-gl=angle', '--use-angle=vulkan']
				: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
		});
	const page = await browser.newPage();
	page.on('console', (m) => {
		const t = m.text();
		if (process.env.RTC_VERBOSE && !t.includes('RTC|')) { console.log('[console] ' + t.slice(0, 200)); }
		if (!t.includes('RTC|') && /ERROR|SCRIPT ERROR|WARNING|Uncaught|abort|RuntimeError/i.test(t)) {
			console.log('[browser-err] ' + t);
		}
	});
	page.on('pageerror', (e) => console.log('[pageerror] ' + e.message));

	await page.goto(url, { waitUntil: 'domcontentloaded' });
	// Report cross-origin isolation, since a threaded export silently fails without it.
	try {
		console.log(`[runner] crossOriginIsolated=${await page.evaluate(() => window.crossOriginIsolated)}`);
	} catch { /* page may still be booting */ }

	await pollUntilDone(async () => {
		try {
			return await page.evaluate(() => ({
				lines: window.__rtc_lines || [], done: window.__rtc_done === true,
			}));
		} catch {
			return null; // navigation in flight
		}
	});
	// The shader-bake counters, so run_tests.mjs can assert a baked export really ran baked.
	try {
		const st = await page.evaluate(() => window.godotWebGPUShaderStats || null);
		if (st) {
			console.log(`[runner] shaderStats baked=${st.baked} translated=${st.translated}`);
		}
		else { console.log('[runner] shaderStats not exposed; window keys: ' + (await page.evaluate(() => Object.keys(window).filter((k) => /godot|Godot|webgpu/i.test(k)).join(','))).slice(0, 300)); }
	} catch (e) { console.log('[runner] shaderStats read failed: ' + e.message.slice(0, 120)); }
	await browser.close();
}

function osa(js) {
	const escaped = js.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
	const script = `tell application "Safari" to do JavaScript "${escaped}" in front document`;
	try {
		return execSync(`osascript -e '${script.replace(/'/g, "'\"'\"'")}'`,
			{ timeout: 10000, encoding: 'utf-8' }).trim();
	} catch {
		return null;
	}
}

function runSafari() {
	if (process.platform !== 'darwin') {
		console.error('[runner] RTC_BROWSER=safari needs macOS');
		process.exit(2);
	}
	execSync(`osascript -e 'tell application "Safari"' -e 'activate'`
		+ ` -e 'if (count of windows) = 0 then make new document'`
		+ ` -e 'set URL of front document to "${url}"' -e 'end tell'`);
	return pollUntilDone(async () => {
		const raw = osa('JSON.stringify({lines: window.__rtc_lines || [], done: window.__rtc_done === true})');
		if (!raw) {
			return null;
		}
		try {
			return JSON.parse(raw);
		} catch {
			// An empty reply means "Allow JavaScript from Apple Events" is off,
			// which otherwise looks exactly like a page that never booted.
			console.log(`[runner] unreadable Safari reply: ${raw.slice(0, 200)}`);
			return null;
		}
	});
}

if (BROWSER === 'safari') {
	await runSafari();
} else {
	await runPlaywright(BROWSER);
}
server.close();
process.exit(seen.some((l) => l.includes('END')) ? 0 : 1);
