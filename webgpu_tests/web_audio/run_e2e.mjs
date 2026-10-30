/**
 * Web audio end to end: runs fixture_project natively (headless, Stream playback) and in Chrome from ./export.sh's
 * exports (Sample playback by default, plus explicit Stream players). main.gd checks itself — finish times and
 * positions through loops, seeks, pitch and pause; Ogg/MP3 samples; bus layouts and runtime bus edits; 2D, 3D and
 * polyphonic players; Stream playback with a capture effect, a generator and a randomizer — and prints an
 * [AUDIO-CHECK] verdict. Here the web verdict must be PASS, every boolean check must agree with the native run, and
 * the page must log no error or warning.
 *
 * Numbers are checked against what native playback does by definition, not against the native run: headless
 * native uses the dummy audio driver, whose positions wander by up to 100 ms between runs. Sample-exact parity of
 * the loop modes and the mixer is run_tests.mjs's job.
 *
 * The nothreads export is served without COOP/COEP headers, as such exports usually are (TASKS.md §3 trap 22),
 * the threaded one with them.
 *
 * Usage: ./export.sh && node run_e2e.mjs [--only nothreads|threads] [--chrome <path>] [--console]
 */

import { spawnSync } from 'child_process';
import { createServer } from 'http';
import { existsSync, readFileSync, statSync } from 'fs';
import { dirname, extname, join } from 'path';
import { fileURLToPath } from 'url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..');
const args = process.argv.slice(2);
const option = (name) => {
	const i = args.indexOf(name);
	return i >= 0 ? args[i + 1] : undefined;
};
const only = option('--only');
const echoConsole = args.includes('--console'); // Echo the page's console, for triage.
const chromePath = option('--chrome') ?? '/usr/bin/google-chrome-stable';
const editor = process.env.EDITOR_BIN ?? join(root, 'bin/godot.linuxbsd.editor.x86_64');

function parse(lines) {
	const results = {};
	for (const line of lines) {
		const m = line.match(/AUDIOTEST (\S+) (.*)$/);
		if (m) {
			results[m[1]] = m[2].trim();
		}
	}
	return results;
}

function runNative() {
	const run = spawnSync(editor, ['--headless', '--path', join(here, 'fixture_project')], { encoding: 'utf8', timeout: 120000 });
	const results = parse((run.stdout ?? '').split('\n'));
	results.warnings = `${run.stdout}\n${run.stderr}`.split('\n').filter((l) => l.startsWith('WARNING:'));
	return results;
}

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
function serve(dir, isolated) {
	return new Promise((resolve) => {
		const server = createServer((req, res) => {
			const path = join(dir, decodeURIComponent(new URL(req.url, 'http://x').pathname));
			if (!existsSync(path) || !statSync(path).isFile()) {
				res.writeHead(404);
				res.end();
				return;
			}
			const headers = { 'Content-Type': MIME[extname(path)] ?? 'application/octet-stream' };
			if (isolated) {
				headers['Cross-Origin-Opener-Policy'] = 'same-origin';
				headers['Cross-Origin-Embedder-Policy'] = 'require-corp';
			}
			res.writeHead(200, headers);
			res.end(readFileSync(path));
		});
		server.listen(0, '127.0.0.1', () => resolve(server));
	});
}

async function runWeb(browser, name, isolated) {
	const dir = join(here, 'exports', name);
	if (!existsSync(join(dir, 'index.html'))) {
		return null;
	}
	const server = await serve(dir, isolated);
	const page = await browser.newPage();
	const lines = [];
	const errors = [];
	page.on('console', (msg) => {
		lines.push(msg.text());
		if (echoConsole) {
			console.log(`    [${name}] ${msg.type()}: ${msg.text().slice(0, 400)}`);
		}
		// The engine's warnings fail too (WebLogger sends them to console.warn as "WARNING: ..."); a native run
		// of this project prints none. The browser's own notices (the WebGL probe's lost context) do not.
		if (msg.type() === 'error' || (msg.type() === 'warning' && msg.text().includes('WARNING:'))) {
			errors.push(`${msg.type()}: ${msg.text()}`);
		}
	});
	page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));
	await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
	const deadline = Date.now() + 90000;
	// Web Audio waits for a user gesture; main.gd waits for the audio clock to move.
	await page.waitForSelector('#canvas');
	await page.waitForTimeout(2000);
	await page.click('#canvas').catch(() => {});
	while (Date.now() < deadline && !lines.some((l) => l.includes('AUDIOTEST done'))) {
		await page.waitForTimeout(250);
	}
	await page.close();
	server.close();
	const verdict = lines.find((l) => l.includes('[AUDIO-CHECK]')) ?? '';
	return { results: parse(lines), errors, verdict };
}

// Engine warnings a web run must print exactly once (main.gd's _limitation_warnings()); any other fails.
const EXPECTED_WARNINGS = [
	{ name: 'bus effects bypassed by Sample playback', pattern: 'Bus "Fx" has audio effects' },
	{ name: 'long stream decoded whole as a Sample', pattern: 'plays as a Sample: on the web that decodes the whole stream' },
];

function compare(label, native, web) {
	let failed = 0;
	for (const expected of EXPECTED_WARNINGS) {
		const count = web.errors.filter((e) => e.includes(expected.pattern)).length;
		const pass = count === 1;
		failed += pass ? 0 : 1;
		console.log(`${pass ? 'PASS' : 'FAIL'}  [${label}] warns once: ${expected.name} (${count}x)`);
	}
	web.errors = web.errors.filter((e) => !EXPECTED_WARNINGS.some((w) => e.includes(w.pattern) || e.startsWith('warning:    at:')));
	// main.gd checks the numbers itself; here the booleans must also agree with the native run.
	for (const key of Object.keys(native).filter((k) => k !== 'warnings')) {
		const n = native[key];
		const w = web.results[key];
		const isBool = n === 'true' || n === 'false';
		const pass = w !== undefined && (!isBool || n === w);
		failed += pass ? 0 : 1;
		console.log(`${pass ? 'PASS' : 'FAIL'}  [${label}] ${key}: web ${w ?? '(missing)'}, native ${n}`);
	}
	if (!web.verdict.includes('PASS')) {
		failed++;
	}
	console.log(`${web.verdict.includes('PASS') ? 'PASS' : 'FAIL'}  [${label}] ${web.verdict || '(no [AUDIO-CHECK] verdict)'}`);
	for (const e of web.errors) {
		failed++;
		console.log(`FAIL  [${label}] console ${e.slice(0, 300)}`);
	}
	return failed;
}

const native = runNative();
if (native.warnings.length) {
	console.error('native run printed warnings:', native.warnings);
	process.exit(1);
}
if (!native.done) {
	console.error('native run did not finish; got:', native);
	process.exit(1);
}

const { chromium } = await import('playwright');
// Headed system Chrome with the Vulkan flags, as every real-GPU tier launches it (TASKS §3 trap 43): headless Chrome
// lost its WebGPU adapter entirely with Chrome 156.0.8078.79 ("WebGPU adapter not found", 2026-10-30). Run under
// xvfb-run (local_ci.sh does) to keep the window off the desktop.
const browser = await chromium.launch({
	headless: !!process.env.CI,
	executablePath: existsSync(chromePath) ? chromePath : undefined,
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
});
let failed = 0;
let ran = 0;
for (const [name, isolated] of [['nothreads', false], ['threads', true]]) {
	if (only && only !== name) {
		continue;
	}
	const web = await runWeb(browser, name, isolated);
	if (!web) {
		console.log(`SKIP  [${name}] no export (run ./export.sh)`);
		continue;
	}
	ran++;
	failed += compare(name, native, web);
}
await browser.close();
console.log(failed ? `\n${failed} failure(s)` : `\nall passed (${ran} export(s))`);
process.exit(failed || !ran ? 1 : 0);
