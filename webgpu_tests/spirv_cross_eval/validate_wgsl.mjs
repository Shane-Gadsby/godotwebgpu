// Validate directories of WGSL in real Chrome WebGPU: createShaderModule + getCompilationInfo per file.
//
// System Chrome, headed, Vulkan flags (CLAUDE.md, §3 trap 43): the device is requested with every optional feature
// the driver may enable and the adapter's own maxSampledTexturesPerShaderStage, as engine.js does.
// Writes { <dir>: { <file>: { ok, errs, scopeErr } } } to $OUT (default ./validation.json).
//
// Usage: node validate_wgsl.mjs <dir> [<dir> ...]

import { createRequire } from 'module';
import fs from 'fs';
import http from 'http';
import path from 'path';
import { fileURLToPath } from 'url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, '..', 'scene_smoketest', 'package.json'));
const { chromium } = require('playwright');

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

const browser = await chromium.launch({
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
console.error(`adapter: ${JSON.stringify(adapter)}`);

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
