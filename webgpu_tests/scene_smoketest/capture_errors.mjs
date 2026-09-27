/**
 * Full-text WebGPU error capture for one already-exported scene.
 *
 * run_scenes.mjs deliberately keeps its per-scene output to one line, truncating
 * each console message to 200 characters and printing ~100 of them. That is fine
 * for a pass/fail tier and useless for a diagnosis: Dawn's real information is in
 * the "While validating … / While encoding … / While calling …" chain that follows
 * the first line, and it gets cut off. Rather than re-learning that (twice, so far
 * — see webgpu_notes/TASKS.md Task 44), point this at an export directory and get
 * every distinct message in full, with a repeat count.
 *
 * Usage:
 *   node capture_errors.mjs <export-dir> [--browser chrome|firefox]
 *                           [--wait <ms>] [--chars <n>] [--all]
 *
 * Options:
 *   --browser <name>   chrome (default) or firefox
 *   --wait <ms>        How long to let the scene run (default: 25000)
 *   --chars <n>        Characters kept per message (default: 2000)
 *   --all              Print every console message, not just error-shaped ones
 *
 * Env vars (Chrome only) — these mirror run_scenes.mjs exactly, and mirroring them
 * matters: the adapter decides the limits and therefore the errors. On a plain
 * `--enable-unsafe-webgpu` launch, demo_3d_particles reports
 * "The number of storage textures (6) in the Compute stage exceeds the maximum
 * per-stage limit (4)" and never reaches the mismatch the harness sees.
 *   CI=1               Headless bundled Chromium on swiftshader (software).
 *   WEBGPU_REAL_GPU=1  Headless bundled Chromium on the real Vulkan adapter.
 *   (neither)          The system's own Chrome, headed — what run_scenes.mjs
 *                      defaults to, and what the recorded results were measured on.
 *
 * Exit codes:
 *   0 = ran, whatever it found     1 = could not run (bad directory, no browser)
 */

import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname, resolve } from 'path';

const MIME_TYPES = {
	'.html': 'text/html',
	'.js': 'text/javascript',
	'.mjs': 'text/javascript',
	'.wasm': 'application/wasm',
	'.pck': 'application/octet-stream',
	'.png': 'image/png',
	'.jpg': 'image/jpeg',
	'.svg': 'image/svg+xml',
	'.json': 'application/json',
	'.ico': 'image/x-icon',
	'.side.wasm': 'application/wasm',
};

// Messages worth reading: Dawn validation output, the engine's own uncaptured-error
// prints, and anything that calls itself an error or a failure.
const ERROR_SHAPE = /GPUValidationError|GPUOutOfMemoryError|GPUInternalError|uncaptured|device lost|Device lost|\berror\b|\bfailed\b|\bInvalid\b/i;

function startServer(dir) {
	return new Promise((resolve_) => {
		const server = createServer((req, res) => {
			const url = req.url.split('?')[0];
			const filePath = join(dir, url === '/' ? 'index.html' : url);
			if (!existsSync(filePath) || statSync(filePath).isDirectory()) {
				res.writeHead(404);
				res.end('Not found');
				return;
			}
			res.writeHead(200, {
				'Content-Type': MIME_TYPES[extname(filePath)] || 'application/octet-stream',
				// The nothreads template does not need these, the threads one does;
				// sending them always costs nothing and removes one failure mode.
				'Cross-Origin-Opener-Policy': 'same-origin',
				'Cross-Origin-Embedder-Policy': 'require-corp',
			});
			res.end(readFileSync(filePath));
		});
		server.listen(0, '127.0.0.1', () => resolve_({ server, url: `http://127.0.0.1:${server.address().port}` }));
	});
}

async function launchChrome() {
	const pw = await import('playwright');
	if (process.env.WEBGPU_REAL_GPU) {
		return pw.chromium.launch({
			headless: true,
			args: [
				'--enable-unsafe-webgpu',
				'--enable-features=Vulkan',
				'--use-vulkan',
				'--use-angle=vulkan',
				'--use-vulkan=native',
				'--ignore-gpu-blocklist',
				'--no-sandbox',
			],
		});
	}
	if (process.env.CI) {
		return pw.chromium.launch({
			headless: true,
			args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer', '--use-angle=swiftshader', '--enable-gpu'],
		});
	}
	// System Chrome, headed — run_scenes.mjs's default, and the adapter every
	// recorded scene result on this machine was measured on.
	return pw.chromium.launch({
		headless: false,
		channel: 'chrome',
		args: ['--use-vulkan', '--enable-features=Vulkan', '--ignore-gpu-blocklist'],
	});
}

async function launchFirefox() {
	const pw = await import('playwright');
	return pw.firefox.launch({
		headless: !!process.env.CI,
		firefoxUserPrefs: { 'dom.webgpu.enabled': true, 'gfx.webgpu.force-enabled': true },
	});
}

async function main() {
	const args = process.argv.slice(2);
	const dir = args.find((a) => !a.startsWith('--'));
	if (!dir) {
		console.error('Usage: node capture_errors.mjs <export-dir> [--browser chrome|firefox] [--wait <ms>] [--chars <n>] [--all]');
		process.exit(1);
	}
	const exportDir = resolve(dir);
	if (!existsSync(join(exportDir, 'index.html'))) {
		console.error(`No index.html in ${exportDir} — export the scene first (run_scenes.mjs --export-only --scenes <id>).`);
		process.exit(1);
	}
	const browserName = args.includes('--browser') ? args[args.indexOf('--browser') + 1] : 'chrome';
	const waitMs = args.includes('--wait') ? parseInt(args[args.indexOf('--wait') + 1], 10) : 25000;
	const chars = args.includes('--chars') ? parseInt(args[args.indexOf('--chars') + 1], 10) : 2000;
	const everything = args.includes('--all');

	const { server, url } = await startServer(exportDir);
	let browser;
	try {
		browser = browserName === 'firefox' ? await launchFirefox() : await launchChrome();
	} catch (e) {
		console.error(`Could not launch ${browserName}: ${e.message}`);
		console.error(`  Install with: npx playwright install ${browserName === 'firefox' ? 'firefox' : 'chromium'}`);
		server.close();
		process.exit(1);
	}

	const page = await browser.newPage();
	// Keyed on the message text so a 500-times-repeated cascade prints once with a
	// count, which is what makes the root error visible at all.
	const seen = new Map();
	const record = (text) => {
		const key = text.substring(0, chars);
		seen.set(key, (seen.get(key) || 0) + 1);
	};
	page.on('console', (msg) => {
		const text = msg.text();
		if (everything || ERROR_SHAPE.test(text)) {
			record(text);
		}
	});
	page.on('pageerror', (e) => record(`[pageerror] ${e.message}`));

	await page.goto(url);
	await page.waitForTimeout(waitMs);

	console.log(`${exportDir}  (${browserName}, ${waitMs}ms)`);
	console.log(`${seen.size} distinct message(s)\n`);
	// Highest repeat count last: the first error is usually the cause and the
	// thousand that follow are its cascade, so reading top-down reads causally.
	const ordered = [...seen.entries()].sort((a, b) => a[1] - b[1]);
	for (const [text, count] of ordered) {
		console.log(`--- x${count} ---`);
		console.log(text);
		console.log('');
	}

	await browser.close();
	server.close();
}

main();
