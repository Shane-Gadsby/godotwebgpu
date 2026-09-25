/**
 * Specialization-Constant Override Test
 *
 * Covers the path that lets a pipeline specialize a shader through WebGPU's own
 * pipeline constants instead of re-patching and re-converting its SPIR-V for
 * every value combination (webgpu_notes/TASKS.md Task 25):
 *
 *   1. Offline half — run a spec-constant fixture through tint_convert_cli and
 *      assert its constants survive as `@id(N) override` declarations, and that
 *      a fixture whose constants *cannot* be overrides is still frozen (the
 *      guard in spirv_preprocess::spec_constants_overridable()).
 *   2. Browser half — hand that same WGSL to a real WebGPU implementation, build
 *      a compute pipeline from it twice (once with no constants, once with real
 *      values for @id(0)/@id(1)/@id(2)) and check the shader's output actually
 *      changes. This is what compute_pipeline_create()'s override path does, so
 *      a pass means the WGPUConstantEntry plumbing genuinely works.
 *
 * Overrides are resolved by the implementation at pipeline-creation time, so a
 * software adapter exercises this mechanism as faithfully as hardware does; the
 * run prints which adapter it got either way. The browser half needs Playwright
 * plus a WebGPU-capable browser — without one the offline half still runs and
 * the suite reports what it skipped.
 *
 * Usage:
 *   node run_tests.mjs
 *   WEBGPU_REAL_GPU=1 node run_tests.mjs         Hardware adapter, not swiftshader
 *   WEBGPU_TEST_BROWSER=/path/to/chrome node run_tests.mjs
 *                                               Use this browser instead of
 *                                               Playwright's bundled one
 */

import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath } from 'url';
import { execFileSync } from 'child_process';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const REPO_ROOT = join(__dirname, '..', '..');
const FIXTURES_DIR = join(REPO_ROOT, 'webgpu_tests', 'shader_corpus', 'fixtures');

// chained_spec_ops.comp: SPEC_A=10, SPEC_B=20, SPEC_C=3, then
//   v   = SPEC_A + SPEC_B
//   v_1 = v * SPEC_C
//   v_2 = v_1 & 0xFF
//   v_3 = v_2 >> 2
// written to values[0..3]. Both expectations below are that arithmetic, so a
// wrong value points at the override chain, not at the test.
const DEFAULT_EXPECTED = [30, 90, 90, 22];
const OVERRIDE_VALUES = { 0: 100, 1: 200, 2: 5 };
const OVERRIDE_EXPECTED = [300, 1500, 220, 55];

let passed = 0;
let failed = 0;

function ok(msg) {
    console.log(`  PASS  ${msg}`);
    passed++;
}

function fail(msg, detail) {
    console.log(`  FAIL  ${msg}`);
    if (detail !== undefined) {
        console.log(`        ${typeof detail === 'string' ? detail : JSON.stringify(detail)}`);
    }
    failed++;
}

function assert(cond, msg, detail) {
    if (cond) {
        ok(msg);
    } else {
        fail(msg, detail);
    }
}

function findTintCli() {
    for (const name of ['tint_convert_cli', 'tint_convert_cli.exe']) {
        const p = join(REPO_ROOT, 'bin', name);
        if (existsSync(p)) return p;
    }
    return null;
}

function convert(tintCli, fixture) {
    const spv = join(FIXTURES_DIR, fixture);
    if (!existsSync(spv)) return { error: `fixture not found: ${spv}` };
    try {
        return { wgsl: execFileSync(tintCli, [spv], { encoding: 'utf-8', maxBuffer: 64 * 1024 * 1024 }) };
    } catch (e) {
        return { error: String(e.stderr || e.message) };
    }
}

const MIME_TYPES = { '.html': 'text/html', '.js': 'text/javascript' };

function startServer() {
    return new Promise((resolve) => {
        const server = createServer((req, res) => {
            const urlPath = new URL(req.url, 'http://localhost').pathname;
            const filePath = join(__dirname, urlPath === '/' ? 'index.html' : urlPath);
            if (!existsSync(filePath)) {
                res.writeHead(404);
                res.end('Not found');
                return;
            }
            res.writeHead(200, { 'Content-Type': MIME_TYPES[extname(filePath)] || 'application/octet-stream' });
            res.end(readFileSync(filePath));
        });
        server.listen(0, '127.0.0.1', () => {
            resolve({ server, url: `http://127.0.0.1:${server.address().port}` });
        });
    });
}

async function runOnGpu(wgsl) {
    let chromium;
    try {
        ({ chromium } = await import('playwright'));
    } catch {
        return { skipped: 'playwright is not installed (npm install)' };
    }

    // Explicit args rather than Playwright's defaults, which inject
    // --enable-unsafe-swiftshader and fight a real Vulkan driver — the same
    // reasoning as scene_smoketest/run_scenes.mjs's launchChrome().
    const launchOpts = process.env.CI && !process.env.WEBGPU_REAL_GPU
        ? {
            headless: true,
            args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer', '--use-angle=swiftshader', '--enable-gpu'],
        }
        : {
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
        };
    // WEBGPU_TEST_BROWSER points at a Chromium/Chrome executable to use instead
    // of Playwright's bundled one — needed wherever the installed playwright
    // package and the available browser build don't match.
    if (process.env.WEBGPU_TEST_BROWSER) {
        launchOpts.executablePath = process.env.WEBGPU_TEST_BROWSER;
    }

    let browser;
    try {
        browser = await chromium.launch(launchOpts);
    } catch (e) {
        return { skipped: `could not launch a browser: ${e.message.split('\n')[0]}` };
    }

    const { server, url } = await startServer();
    try {
        const page = await browser.newPage();
        const consoleErrors = [];
        page.on('console', (m) => {
            // The browser's automatic /favicon.ico probe 404s against this
            // minimal server; it says nothing about the test.
            if (m.type() === 'error' && !m.text().includes('404')) consoleErrors.push(m.text());
        });
        await page.goto(url, { waitUntil: 'load' });
        const result = await page.evaluate((code) => window.runOverrideTest(code), wgsl);
        return { result, consoleErrors };
    } finally {
        server.close();
        await browser.close();
    }
}

function arraysEqual(a, b) {
    return Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v, i) => v === b[i]);
}

async function main() {
    console.log('╔══════════════════════════════════════════════════════════╗');
    console.log('║   Specialization-Constant Override Test                  ║');
    console.log('╚══════════════════════════════════════════════════════════╝\n');

    const tintCli = findTintCli();
    if (!tintCli) {
        console.log('NOTE: bin/tint_convert_cli not found — build it with');
        console.log('      ./drivers/webgpu/tint_cli/build.sh\n');
        console.log('PASS (skipped — no Tint CLI available)\n');
        process.exit(0);
    }
    console.log(`Using Tint CLI: ${tintCli}\n`);

    console.log('Offline: SPIR-V → WGSL');
    const chained = convert(tintCli, 'chained_spec_ops.spv');
    assert(chained.wgsl !== undefined, 'chained_spec_ops.spv converts', chained.error);

    let wgsl = null;
    if (chained.wgsl) {
        wgsl = chained.wgsl;
        const ids = [...wgsl.matchAll(/@id\((\d+)\)/g)].map((m) => Number(m[1])).sort();
        assert(arraysEqual(ids, [0, 1, 2]), 'its three spec constants survive as @id(0..2) overrides', ids);
        assert(/override\s+\w+\s*:\s*u32\s*=\s*\(?\s*SPEC_A/.test(wgsl) || wgsl.includes('SPEC_A + SPEC_B'),
            'a derived OpSpecConstantOp stays an override expression, not a folded literal');
    }

    // The guard's other half: a module that cannot use overrides must still be
    // frozen, or Tint raises an internal error on it.
    const specConstants = convert(tintCli, 'spec_constants.spv');
    assert(specConstants.wgsl !== undefined, 'spec_constants.spv converts', specConstants.error);
    if (specConstants.wgsl) {
        assert(!specConstants.wgsl.includes('@id('),
            'a module building a composite from a spec constant is still frozen');
    }

    console.log('\nBrowser: pipeline constants');
    if (!wgsl) {
        console.log('  SKIP  no WGSL to run (conversion failed above)');
    } else {
        const { result, skipped, consoleErrors } = await runOnGpu(wgsl);
        if (skipped) {
            console.log(`  SKIP  ${skipped}`);
            console.log('        Offline assertions above still ran.');
        } else if (!result.ok) {
            fail('override pipeline runs on a real GPU', result.error || result);
            if (result.compileErrors) {
                for (const e of result.compileErrors) console.log(`        wgsl: ${e}`);
            }
        } else {
            console.log(`  Adapter: ${result.adapterInfo}`);
            assert(arraysEqual(result.defaults, DEFAULT_EXPECTED),
                'with no pipeline constants every override keeps its default',
                { got: result.defaults, want: DEFAULT_EXPECTED });
            assert(arraysEqual(result.overridden, OVERRIDE_EXPECTED),
                `pipeline constants ${JSON.stringify(OVERRIDE_VALUES)} reach the shader`,
                { got: result.overridden, want: OVERRIDE_EXPECTED });
            assert(result.errors.length === 0, 'no uncaptured WebGPU errors', result.errors);
            if (consoleErrors.length > 0) {
                console.log(`        (browser console errors: ${JSON.stringify(consoleErrors)})`);
            }
        }
    }

    console.log('\n────────────────────────────────────────────────────────────');
    console.log(`Results: ${passed} passed, ${failed} failed\n`);
    process.exit(failed === 0 ? 0 : 1);
}

main().catch((e) => {
    console.error(e);
    process.exit(1);
});
