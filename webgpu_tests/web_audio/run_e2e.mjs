/**
 * Web audio end-to-end parity: runs fixture_project in Chrome from ./export.sh's exports (Sample playback) and
 * natively (Stream playback, headless), and checks the AUDIOTEST lines main.gd prints: finish times, playback
 * positions through loops, seeks, pitch and pause, and that bus edits, 3D players and polyphonic streams keep
 * working. Fails on any page error or console error, too.
 *
 * Numbers are checked against what native playback does by definition (EXPECTED), not against the native run:
 * headless native uses the dummy audio driver, whose positions wander by up to 100 ms between runs. The native
 * run's numbers are printed alongside; its booleans must match the web's. Sample-exact native parity of the loop
 * modes and the mixer is run_tests.mjs's job.
 *
 * The nothreads export is served without COOP/COEP headers, as such exports usually are (TASKS.md §3 trap 22),
 * the threaded one with them.
 *
 * Usage: ./export.sh && node run_e2e.mjs [--only nothreads|threads] [--chrome <path>]
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
const chromePath = option('--chrome') ?? '/usr/bin/google-chrome-stable';
const editor = process.env.EDITOR_BIN ?? join(root, 'bin/godot.linuxbsd.editor.x86_64');

// Seconds, for main.gd's streams: 0.4 s long; loops between 0.1 and 0.3 s.
const EXPECTED = {
	'finish_pitch_1': 0.4,
	'finish_pitch_2': 0.2,
	'position_plain_0.25': 0.25,
	'position_seek_0.2_plus_0.1': 0.3,
	'position_forward_0.45': 0.25, // 0.1 + (0.45 - 0.1) % 0.2
	'position_forward_pitch_2_0.3': 0.2, // 0.6 s of stream: 0.1 + (0.6 - 0.1) % 0.2
	'position_pingpong_0.45': 0.15, // Up to 0.3, then 0.15 back down.
	'position_backward_seek_0.25_plus_0.1': 0.15, // Down from 0.25.
};
// Finished is signaled on the main loop after the end; positions are read there too.
const TOLERANCE = { finish: 0.05, position: 0.03 };

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
	return parse((run.stdout ?? '').split('\n'));
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
		if (msg.type() === 'error') {
			errors.push(msg.text());
		}
	});
	page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));
	await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
	const deadline = Date.now() + 90000;
	while (Date.now() < deadline && !lines.some((l) => l.includes('AUDIOTEST done'))) {
		await page.waitForTimeout(250);
	}
	await page.close();
	server.close();
	return { results: parse(lines), errors };
}

function compare(label, native, web) {
	let failed = 0;
	for (const key of Object.keys(native)) {
		const n = native[key];
		const w = web.results[key];
		let pass;
		let reference = `native ${n}`;
		if (w === undefined) {
			pass = false;
		} else if (key in EXPECTED) {
			const tolerance = key.startsWith('finish') ? TOLERANCE.finish : TOLERANCE.position;
			pass = Math.abs(Number(w) - EXPECTED[key]) <= tolerance;
			reference = `expected ${EXPECTED[key]} ±${tolerance} (headless native ${n})`;
		} else {
			pass = n === w;
		}
		failed += pass ? 0 : 1;
		console.log(`${pass ? 'PASS' : 'FAIL'}  [${label}] ${key}: web ${w ?? '(missing)'}, ${reference}`);
	}
	for (const e of web.errors) {
		failed++;
		console.log(`FAIL  [${label}] console error: ${e.slice(0, 300)}`);
	}
	return failed;
}

const native = runNative();
if (!native.done) {
	console.error('native run did not finish; got:', native);
	process.exit(1);
}

const { chromium } = await import('playwright');
const browser = await chromium.launch({
	headless: true,
	executablePath: existsSync(chromePath) ? chromePath : undefined,
	args: ['--autoplay-policy=no-user-gesture-required', '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--use-angle=vulkan', '--ignore-gpu-blocklist'],
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
