/**
 * Web Audio sample driver parity tests.
 *
 * Loads GodotSampleDriver straight from platform/web/js/libs/library_godot_audio.js (no engine build, no
 * export) into a Chrome page and runs sample_driver_tests.js there: every test renders the driver in an
 * OfflineAudioContext and compares the samples with ports of the native AudioServer / AudioStreamPlaybackWAV
 * behavior (loop modes, volume ramps and fades, bus sends/solo/mute/move, positions, the 3D high shelf).
 *
 * Usage:
 *   node run_tests.mjs [--filter <substring>] [--chrome <path>]
 * Needs Playwright (npm install here); uses system Chrome when present, Playwright's Chromium otherwise.
 */

import { existsSync, readFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..');

const args = process.argv.slice(2);
const option = (name) => {
	const i = args.indexOf(name);
	return i >= 0 ? args[i + 1] : undefined;
};
const filter = option('--filter');
const chromePath = option('--chrome') ?? ['/usr/bin/google-chrome-stable', '/usr/bin/google-chrome'].find((p) => existsSync(p));

const { chromium } = await import('playwright');
const browser = await chromium.launch({
	headless: true,
	...(chromePath ? { executablePath: chromePath } : {}),
	args: ['--autoplay-policy=no-user-gesture-required'],
});
const page = await browser.newPage();
page.on('pageerror', (e) => console.error('page error:', e.message));
await page.goto('about:blank');

// The library file is an Emscripten JS library: stub the few globals it uses at load time.
await page.addScriptTag({
	content: `
		var LibraryManager = { library: {} };
		function mergeInto(library, object) { Object.assign(library, object); }
		function autoAddDeps() {}
	`,
});
await page.addScriptTag({ content: readFileSync(join(root, 'platform/web/js/libs/library_godot_audio.js'), 'utf8') });
await page.addScriptTag({ content: readFileSync(join(here, 'sample_driver_tests.js'), 'utf8') });

const results = await page.evaluate((f) => runTests(f), filter);
await browser.close();

let failed = 0;
for (const r of results) {
	if (!r.pass) {
		failed++;
	}
	console.log(`${r.pass ? 'PASS' : 'FAIL'}  ${r.name}\n      ${r.detail}`);
}
console.log(`\n${results.length - failed} / ${results.length} passed`);
process.exit(failed || results.length === 0 ? 1 : 0);
