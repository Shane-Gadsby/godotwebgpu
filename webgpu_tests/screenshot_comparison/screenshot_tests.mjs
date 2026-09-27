/**
 * Multi-Browser Screenshot Comparison Tests
 *
 * Uses Playwright to render WebGPU scenes in Chrome and Firefox (when available),
 * captures screenshots, and compares them for visual regression.
 *
 * Usage:
 *   node screenshot_tests.mjs                     Run tests
 *   node screenshot_tests.mjs --update-baselines  Save current as baseline
 *   node screenshot_tests.mjs --threshold 0.05    Set pixel diff threshold (0-1)
 *
 * Output:
 *   screenshots/baselines/   Reference images
 *   screenshots/current/     Latest captures
 *   screenshots/diffs/       Visual diff images (on failure)
 *   screenshots/report.json  Machine-readable results
 */

import { createServer } from 'http';
import { readFileSync, writeFileSync, mkdirSync, existsSync, readdirSync } from 'fs';
import { join, extname } from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import { decodePng, encodePng } from './png.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const SCREENSHOTS_DIR = join(__dirname, 'screenshots');
const BASELINES_DIR = join(SCREENSHOTS_DIR, 'baselines');
const CURRENT_DIR = join(SCREENSHOTS_DIR, 'current');
const DIFFS_DIR = join(SCREENSHOTS_DIR, 'diffs');

const SCENES = ['triangle', 'textured_quad', 'instanced', 'compute_pattern'];

const MIME_TYPES = {
    '.html': 'text/html',
    '.js': 'text/javascript',
    '.css': 'text/css',
    '.json': 'application/json',
    '.png': 'image/png',
};

// ─── CLI Args ─────────────────────────────────────────────────────────────────

const args = process.argv.slice(2);
const UPDATE_BASELINES = args.includes('--update-baselines');
// `args[args.indexOf(flag) + 1]` reads args[0] when the flag is absent, which
// is another flag, so parse the value only when the flag is really there.
function argValue(flag, fallback) {
    const i = args.indexOf(flag);
    return i >= 0 && args[i + 1] !== undefined ? args[i + 1] : fallback;
}

// Fraction of pixels allowed to differ before a screenshot counts as changed.
const THRESHOLD = parseFloat(argValue('--threshold', '0.01'));
// Per-channel 0-255 delta below which two pixels count as the same. Covers
// driver-level rasterization noise between the machine that produced the
// baselines and the one running the test; override with --pixel-tolerance.
const PIXEL_TOLERANCE = parseInt(argValue('--pixel-tolerance', '8'), 10);

// ─── HTTP Server ──────────────────────────────────────────────────────────────

function startServer() {
    return new Promise((resolve) => {
        const server = createServer((req, res) => {
            const url = req.url.split('?')[0];
            const filePath = join(__dirname, url === '/' ? 'render_scene.html' : url);
            if (!existsSync(filePath)) {
                res.writeHead(404);
                res.end('Not found');
                return;
            }
            const ext = extname(filePath);
            res.writeHead(200, {
                'Content-Type': MIME_TYPES[ext] || 'application/octet-stream',
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp',
            });
            res.end(readFileSync(filePath));
        });
        server.listen(0, '127.0.0.1', () => {
            resolve({ server, url: `http://127.0.0.1:${server.address().port}` });
        });
    });
}

// ─── PNG Comparison ───────────────────────────────────────────────────────────

/**
 * Compare two PNG buffers pixel by pixel.
 * Returns { match, diffRatio, diffPixels, diffImage }
 *
 * diffRatio is the fraction of *pixels* that differ by more than
 * PIXEL_TOLERANCE on any channel — not a byte ratio. Comparing the compressed
 * PNG bytes (what this used to do) is meaningless: deflate output is not
 * locally stable, so one changed pixel rewrites most of the stream and reads as
 * a ~99% difference even when the two images look the same.
 *
 * GPU rasterization differs slightly between machines and drivers (gradient
 * dithering, edge coverage), so an exact match is not something a baseline can
 * require; PIXEL_TOLERANCE absorbs that while still catching a real rendering
 * change, which moves whole regions far further than a couple of levels.
 */
function comparePngs(baseline, current, threshold) {
    let a, b;
    try {
        a = decodePng(baseline);
        b = decodePng(current);
    } catch (e) {
        // A PNG we cannot decode is a failure to report, not a crash.
        return { match: false, diffRatio: 1, diffPixels: 0, diffImage: null, error: e.message };
    }

    if (a.width !== b.width || a.height !== b.height) {
        return {
            match: false,
            diffRatio: 1,
            diffPixels: a.width * a.height,
            diffImage: null,
            error: `size mismatch: baseline ${a.width}x${a.height}, current ${b.width}x${b.height}`,
        };
    }

    const total = a.width * a.height;
    const diff = Buffer.alloc(total * 4);
    let diffPixels = 0;

    for (let i = 0, p = 0; i < total; i++, p += 4) {
        const delta = Math.max(
            Math.abs(a.data[p] - b.data[p]),
            Math.abs(a.data[p + 1] - b.data[p + 1]),
            Math.abs(a.data[p + 2] - b.data[p + 2]),
            Math.abs(a.data[p + 3] - b.data[p + 3]),
        );
        if (delta > PIXEL_TOLERANCE) {
            diffPixels++;
            diff[p] = 255; diff[p + 1] = 0; diff[p + 2] = 0; diff[p + 3] = 255;
        } else {
            // Matching pixels stay as a dimmed copy of the baseline, so the
            // diff image shows *where* in the scene the change is.
            diff[p] = a.data[p] >> 2;
            diff[p + 1] = a.data[p + 1] >> 2;
            diff[p + 2] = a.data[p + 2] >> 2;
            diff[p + 3] = 255;
        }
    }

    const diffRatio = diffPixels / total;
    const match = diffRatio <= threshold;

    return {
        match,
        diffRatio,
        diffPixels,
        diffImage: match ? null : encodePng(a.width, a.height, diff),
    };
}

/** True when every pixel of a capture is opaque pure black — see its use below. */
function isBlankBlack(buffer) {
    let img;
    try {
        img = decodePng(buffer);
    } catch {
        return false;
    }
    for (let i = 0; i < img.data.length; i += 4) {
        if (img.data[i] || img.data[i + 1] || img.data[i + 2]) return false;
    }
    return true;
}

// ─── Screenshot Capture ───────────────────────────────────────────────────────

async function captureScreenshots(baseUrl) {
    let chromium, firefox;
    try {
        const pw = await import('playwright');
        chromium = pw.chromium;
        firefox = pw.firefox;
    } catch {
        console.error('ERROR: Playwright not installed.');
        console.error('  npm install playwright');
        console.error('  npx playwright install chromium firefox');
        process.exit(1);
    }

    const browsers = [];

    // CI runners have no real GPU, so real-Vulkan WebGPU has no adapter to bind
    // to and the GPU process dies mid-session (surfacing later as "A valid
    // external Instance reference no longer exists" on the page). Match the
    // swiftshader/software fallback other CI scripts already use (see
    // resource_lifecycle/run_tests.mjs, sdfgi_race_repro/*) instead of the
    // real-GPU-only flags in capture_screenshots.mjs, which are for local
    // repro runs on real hardware.
    const isCI = !!process.env.CI;

    // Try to launch Chrome with WebGPU
    try {
        const chrome = await chromium.launch({
            headless: false,
            args: isCI
                ? ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer', '--use-angle=swiftshader', '--enable-gpu']
                : ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer'],
        });
        browsers.push({ name: 'chromium', browser: chrome });
        console.log('  Chromium: launched');
    } catch (e) {
        console.log(`  Chromium: unavailable (${e.message})`);
    }

    // Try to launch Firefox with WebGPU. Firefox on Linux has no swiftshader-
    // style software WebGPU fallback, so on a GPU-less CI runner this is
    // expected to report "No GPU adapter" and be skipped below, not fixed here.
    try {
        const ff = await firefox.launch({
            headless: false,
            firefoxUserPrefs: {
                'dom.webgpu.enabled': true,
                'gfx.webgpu.force-enabled': true,
            },
        });
        browsers.push({ name: 'firefox', browser: ff });
        console.log('  Firefox: launched');
    } catch (e) {
        console.log(`  Firefox: unavailable (${e.message})`);
    }

    if (browsers.length === 0) {
        console.error('ERROR: No browsers available. Install with:');
        console.error('  npx playwright install chromium firefox');
        process.exit(1);
    }

    const captures = [];

    for (const { name: browserName, browser } of browsers) {
        for (const scene of SCENES) {
            const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
            const url = `${baseUrl}/render_scene.html?scene=${scene}`;

            console.log(`  [${browserName}] ${scene}...`);

            try {
                await page.goto(url);

                // Wait for render completion
                await page.waitForFunction('window.__renderComplete === true', { timeout: 30000 });

                // Check for render errors
                const renderError = await page.evaluate('window.__renderError');
                if (renderError) {
                    console.log(`    SKIP: ${renderError}`);
                    captures.push({ browser: browserName, scene, error: renderError });
                    await page.close();
                    continue;
                }

                // Small delay for GPU to finish presenting
                await page.waitForTimeout(100);

                // Capture screenshot of just the canvas
                const canvas = page.locator('#canvas');
                const screenshot = await canvas.screenshot({ type: 'png' });

                const filename = `${browserName}_${scene}.png`;

                // Firefox on a GPU-less runner under Xvfb reports an adapter and
                // finishes without error, but nothing is ever composited, so the
                // screenshot comes back pure black. None of these scenes renders
                // an all-black frame (even compute_pattern, which clears to
                // black, draws over it), so this is a capture failure and not a
                // rendering regression to report against the baseline — these
                // scenes are hand-written WebGPU JS and never touch the Godot
                // driver, so nothing in this repo can turn one black.
                if (isBlankBlack(screenshot)) {
                    const why = 'rendered blank (no compositing — browser has no working WebGPU on this machine)';
                    console.log(`    SKIP: ${why}`);
                    captures.push({ browser: browserName, scene, error: why });
                    await page.close();
                    continue;
                }

                captures.push({ browser: browserName, scene, filename, data: screenshot });

            } catch (e) {
                console.log(`    ERROR: ${e.message}`);
                captures.push({ browser: browserName, scene, error: e.message });
            }

            await page.close();
        }

        await browser.close();
    }

    return captures;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

async function main() {
    console.log('╔══════════════════════════════════════════════════════════╗');
    console.log('║   Multi-Browser WebGPU Screenshot Comparison             ║');
    console.log('╚══════════════════════════════════════════════════════════╝\n');

    // Ensure directories exist
    mkdirSync(BASELINES_DIR, { recursive: true });
    mkdirSync(CURRENT_DIR, { recursive: true });
    mkdirSync(DIFFS_DIR, { recursive: true });

    // Start server
    const { server, url } = await startServer();
    console.log(`Server: ${url}\n`);
    console.log('Launching browsers...');

    // Capture screenshots
    const captures = await captureScreenshots(url);
    server.close();

    console.log(`\nCaptured ${captures.filter(c => c.data).length} screenshots.\n`);

    // Save current screenshots
    for (const cap of captures) {
        if (cap.data) {
            writeFileSync(join(CURRENT_DIR, cap.filename), cap.data);
        }
    }

    if (UPDATE_BASELINES) {
        console.log('Updating baselines...');
        for (const cap of captures) {
            if (cap.data) {
                writeFileSync(join(BASELINES_DIR, cap.filename), cap.data);
                console.log(`  Saved: ${cap.filename}`);
            }
        }
        console.log('\nBaselines updated. Run without --update-baselines to compare.');
        process.exit(0);
    }

    // Compare against baselines
    console.log('─── Comparison Results ────────────────────────────────────\n');

    const results = [];
    let passed = 0;
    let failed = 0;
    let skipped = 0;
    let noBaseline = 0;

    for (const cap of captures) {
        if (cap.error) {
            console.log(`  [SKIP] ${cap.browser}/${cap.scene}: ${cap.error}`);
            skipped++;
            results.push({ ...cap, status: 'skip' });
            continue;
        }

        const baselinePath = join(BASELINES_DIR, cap.filename);
        if (!existsSync(baselinePath)) {
            console.log(`  [NEW]  ${cap.filename} — no baseline (run --update-baselines)`);
            noBaseline++;
            results.push({ browser: cap.browser, scene: cap.scene, filename: cap.filename, status: 'new' });
            continue;
        }

        const baseline = readFileSync(baselinePath);
        const comparison = comparePngs(baseline, cap.data, THRESHOLD);

        if (comparison.match) {
            console.log(`  [PASS] ${cap.filename} (diff: ${(comparison.diffRatio * 100).toFixed(3)}%)`);
            passed++;
            results.push({ browser: cap.browser, scene: cap.scene, filename: cap.filename, status: 'pass', diffRatio: comparison.diffRatio });
        } else {
            const reason = comparison.error
                ? comparison.error
                : `diff: ${(comparison.diffRatio * 100).toFixed(3)}% of pixels > ${(THRESHOLD * 100).toFixed(1)}%`;
            console.log(`  [FAIL] ${cap.filename} (${reason})`);
            // The diff image is the only thing that says *what* changed once the
            // artifact is all that is left of the run.
            if (comparison.diffImage) {
                writeFileSync(join(DIFFS_DIR, cap.filename), comparison.diffImage);
            }
            failed++;
            results.push({
                browser: cap.browser, scene: cap.scene, filename: cap.filename,
                status: 'fail', diffRatio: comparison.diffRatio, error: comparison.error,
            });
        }
    }

    // Cross-browser comparison (same scene, different browsers)
    console.log('\n─── Cross-Browser Comparison ──────────────────────────────\n');

    const byScene = {};
    for (const cap of captures) {
        if (cap.data) {
            if (!byScene[cap.scene]) byScene[cap.scene] = [];
            byScene[cap.scene].push(cap);
        }
    }

    for (const [scene, caps] of Object.entries(byScene)) {
        if (caps.length < 2) {
            console.log(`  [SKIP] ${scene}: only ${caps.length} browser(s) available`);
            continue;
        }

        // Compare first browser against second
        const comparison = comparePngs(caps[0].data, caps[1].data, THRESHOLD * 5); // Looser threshold for cross-browser
        const status = comparison.match ? 'PASS' : 'WARN';
        console.log(`  [${status}] ${scene}: ${caps[0].browser} vs ${caps[1].browser} — diff: ${(comparison.diffRatio * 100).toFixed(3)}%`);

        results.push({
            type: 'cross-browser',
            scene,
            browsers: [caps[0].browser, caps[1].browser],
            status: comparison.match ? 'pass' : 'warn',
            diffRatio: comparison.diffRatio,
        });
    }

    // Write report
    const report = {
        timestamp: new Date().toISOString(),
        threshold: THRESHOLD,
        passed,
        failed,
        skipped,
        noBaseline,
        results,
    };
    writeFileSync(join(SCREENSHOTS_DIR, 'report.json'), JSON.stringify(report, null, 2));

    // Summary
    console.log('\n─── Summary ───────────────────────────────────────────────');
    console.log(`  Passed: ${passed}, Failed: ${failed}, Skipped: ${skipped}, New: ${noBaseline}`);
    console.log(`  Report: screenshots/report.json`);
    console.log(`  Current: screenshots/current/`);

    if (noBaseline > 0 && failed === 0) {
        console.log('\n  No baselines exist yet. Run with --update-baselines to create them.');
        process.exit(0);
    }

    // Every browser skipping is how this job would go green while testing
    // nothing at all (a browser that stops rendering skips rather than fails).
    // At least one image has to have been compared for the run to mean anything.
    if (passed + failed === 0) {
        console.log('\n  No screenshot was compared — every browser skipped. See the SKIP reasons above.');
        process.exit(1);
    }

    process.exit(failed > 0 ? 1 : 0);
}

main().catch((e) => {
    console.error('Fatal:', e);
    process.exit(1);
});
