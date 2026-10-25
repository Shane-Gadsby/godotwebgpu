// Validate directories of WGSL in real Chrome WebGPU: createShaderModule + getCompilationInfo per file.
// BROWSER=firefox validates in Playwright's Firefox instead (naga, not Tint, compiles WGSL there; work task 11).
//
// System Chrome, headed, Vulkan flags (CLAUDE.md, §3 trap 43): the device is requested with every optional feature
// the driver may enable and the adapter's own maxSampledTexturesPerShaderStage, as engine.js does.
// Writes { <dir>: { <file>: { ok, errs, scopeErr } } } to $OUT (default ./validation.json).
//
// Usage: node validate_wgsl.mjs <dir> [<dir> ...]

import { execFileSync } from 'child_process';
import { createRequire } from 'module';
import fs from 'fs';
import http from 'http';
import path from 'path';
import { fileURLToPath } from 'url';
import { FIREFOX_WEBGPU_PREFS } from '../scene_smoketest/firefox_prefs.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
// Playwright comes from scene_smoketest's node_modules. A git worktree has none (node_modules is not tracked), so
// fall back to the main checkout's, found through the shared git directory.
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
	throw new Error(`playwright not found in ${candidates.join(' or ')}/node_modules (npm ci in webgpu_tests/scene_smoketest)`);
}
const { chromium, firefox } = loadPlaywright();
const BROWSER = process.env.BROWSER || 'chrome';

const dirs = process.argv.slice(2);
if (!dirs.length) {
	console.error('usage: node validate_wgsl.mjs <dir> [<dir> ...]');
	process.exit(2);
}

// navigator.gpu needs a secure context; about:blank has none.
const server = http.createServer((req, res) => {
	res.setHeader('content-type', 'text/html');
	res.end('<html><body>wgsl validation</body></html>');
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));

// Firefox needs WebGPU switched on and takes none of Chrome's flags (scene_smoketest/run_scenes.mjs launchFirefox).
const browser = BROWSER === 'firefox' ? await firefox.launch({
	headless: false,
	firefoxUserPrefs: FIREFOX_WEBGPU_PREFS,
}) : await chromium.launch({
	headless: false,
	executablePath: '/usr/bin/google-chrome-stable',
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
});
const page = await browser.newPage();
await page.goto(`http://127.0.0.1:${server.address().port}/`);
const adapter = await page.evaluate(async () => {
	const a = await navigator.gpu.requestAdapter();
	const features = ['shader-f16', 'subgroups', 'clip-distances', 'dual-source-blending'].filter((f) => a.features.has(f));
	window.dev = await a.requestDevice({
		requiredFeatures: features,
		requiredLimits: { maxSampledTexturesPerShaderStage: a.limits.maxSampledTexturesPerShaderStage },
	});
	return { vendor: a.info.vendor, architecture: a.info.architecture, fallback: a.info.isFallbackAdapter, features };
});
console.error(`adapter (${BROWSER}): ${JSON.stringify(adapter)}`);

let anyInvalid = false;
const results = {};
for (const dir of dirs) {
	const out = {};
	const files = fs.readdirSync(dir).filter((f) => f.endsWith('.wgsl')).sort();
	for (const file of files) {
		const code = fs.readFileSync(path.join(dir, file), 'utf8');
		out[file] = await page.evaluate(async (code) => {
			dev.pushErrorScope('validation');
			const module = dev.createShaderModule({ code });
			const info = await module.getCompilationInfo();
			const scope = await dev.popErrorScope();
			const errs = info.messages.filter((m) => m.type === 'error').map((m) => `${m.lineNum}:${m.linePos} ${m.message}`);
			return { ok: errs.length === 0 && !scope, errs: errs.slice(0, 3), scopeErr: scope ? scope.message.slice(0, 400) : null };
		}, code);
	}
	results[dir] = out;
	const invalid = Object.entries(out).filter(([, r]) => !r.ok);
	anyInvalid ||= invalid.length > 0;
	console.error(`${dir}: ${files.length} files, ${invalid.length} invalid`);
	for (const [file, r] of invalid.slice(0, 20)) {
		console.error(`  ${file}: ${(r.errs[0] || r.scopeErr || '').split('\n')[0]}`);
	}
}
fs.writeFileSync(process.env.OUT || 'validation.json', JSON.stringify(results, null, 1));
await browser.close();
server.close();
process.exit(anyInvalid ? 1 : 0);
