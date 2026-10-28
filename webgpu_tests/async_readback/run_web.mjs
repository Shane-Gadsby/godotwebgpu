// Runs an async readback export (export.sh) in a real browser and prints the project's W66RESULT.
//
//   node run_web.mjs [--dir exports/web] [--browser chrome|firefox] [--timeout 90000]
//
// System Chrome, headed, Vulkan flags; Firefox with FIREFOX_WEBGPU_PREFS (CLAUDE.md, work board README). The adapter
// is asserted before the export loads: Chrome must give nvidia/lovelace, not the fallback adapter, with 48 sampled
// textures per stage; Firefox must give an adapter at all. Exit 0 when every check passed, 1 when one failed,
// 3 when the adapter was refused, 4 on a timeout.
import { execFileSync } from 'child_process';
import { createRequire } from 'module';
import fs from 'fs';
import http from 'http';
import path from 'path';
import { fileURLToPath } from 'url';
import { FIREFOX_WEBGPU_PREFS } from '../scene_smoketest/firefox_prefs.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const DIR = path.resolve(here, arg('dir', 'exports/web'));
const BROWSER = arg('browser', 'chrome');
const TIMEOUT = +arg('timeout', 90000);

// Playwright from scene_smoketest's node_modules, or the main checkout's when this is a worktree without one.
function loadPlaywright() {
	const candidates = [path.join(here, '..', 'scene_smoketest')];
	try {
		const common = execFileSync('git', ['-C', here, 'rev-parse', '--path-format=absolute', '--git-common-dir'], { encoding: 'utf8' }).trim();
		candidates.push(path.join(path.dirname(common), 'webgpu_tests', 'scene_smoketest'));
	} catch {
		// Not a git checkout: only the local copy can work.
	}
	for (const dir of candidates) {
		if (fs.existsSync(path.join(dir, 'node_modules', 'playwright'))) {
			return createRequire(path.join(dir, 'package.json'))('playwright');
		}
	}
	throw new Error(`playwright not found in ${candidates.join(' or ')}/node_modules`);
}
const { chromium, firefox } = loadPlaywright();

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.bin': 'application/octet-stream' };
const server = http.createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]);
	if (p === '/__probe.html') {
		r.writeHead(200, { 'Content-Type': 'text/html' });
		r.end('<html><body>adapter probe</body></html>');
		return;
	}
	const f = path.join(DIR, p === '/' ? 'index.html' : p);
	if (!fs.existsSync(f) || !fs.statSync(f).isFile()) {
		r.writeHead(404);
		r.end();
		return;
	}
	r.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
	r.end(fs.readFileSync(f));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/`;

const browser = BROWSER === 'firefox' ? await firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS }) : await chromium.launch({
	headless: false,
	executablePath: '/usr/bin/google-chrome-stable',
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
});

let code = 0;
try {
	// Adapter first, on a blank page of the same origin (navigator.gpu needs a secure context).
	const probe = await browser.newPage();
	await probe.goto(url + '__probe.html');
	const adapter = await probe.evaluate(async () => {
		const a = navigator.gpu ? await navigator.gpu.requestAdapter() : null;
		return a ? { vendor: a.info.vendor, architecture: a.info.architecture, fallback: a.info.isFallbackAdapter, maxSampled: a.limits.maxSampledTexturesPerShaderStage } : null;
	});
	await probe.close();
	console.log(`adapter (${BROWSER}): ${JSON.stringify(adapter)}`);
	const problem = !adapter ? 'no WebGPU adapter' : adapter.fallback ? 'the fallback adapter' : BROWSER === 'firefox' ? null
		: `${adapter.vendor}/${adapter.architecture}` !== 'nvidia/lovelace' ? `${adapter.vendor}/${adapter.architecture}, not nvidia/lovelace`
		: adapter.maxSampled < 48 ? `${adapter.maxSampled} sampled textures per stage, not 48` : null;
	if (problem) {
		console.log(`ADAPTER REFUSED: ${problem}`);
		code = 3;
	} else {
		const page = await browser.newPage();
		const lines = [];
		let result = null;
		const finished = new Promise((resolve) => {
			page.on('console', (m) => {
				const t = m.text();
				if (/^W66|ERROR|error/.test(t)) {
					lines.push(`[${m.type()}] ${t.split('\n')[0]}`);
				}
				if (t.startsWith('W66RESULT ')) {
					result = JSON.parse(t.slice('W66RESULT '.length));
				}
				if (t.startsWith('W66DONE')) {
					resolve(true);
				}
			});
		});
		page.on('pageerror', (e) => lines.push(`[pageerror] ${e.message}`));
		await page.goto(url);
		const ok = await Promise.race([finished, new Promise((r) => setTimeout(() => r(false), TIMEOUT))]);
		for (const l of lines.filter((x) => !x.includes('W66RESULT')).slice(0, 60)) {
			console.log(l);
		}
		if (!ok || !result) {
			console.log('TIMEOUT: no W66DONE');
			code = 4;
		} else {
			console.log(`W66RESULT ${JSON.stringify(result)}`);
			const failed = Object.entries(result.results).filter(([, v]) => !v.ok);
			for (const [k, v] of failed) {
				console.log(`FAIL ${k}: ${v.error}`);
			}
			console.log(`${result.passed}/${result.total} passed (${BROWSER})`);
			code = failed.length ? 1 : 0;
		}
	}
} finally {
	await browser.close();
	server.close();
}
process.exit(code);
