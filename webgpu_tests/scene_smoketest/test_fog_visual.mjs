/**
 * Volumetric fog smoothness test.
 *
 * Guards the froxel-sampling regression reported under TASKS.md Task 12.1
 * ("WebGPU volumetric fog looked blockier than native"). That report sat open
 * and uninvestigated for a week and then turned out not to reproduce -- but
 * nothing in the suite would have caught it if it had been real, which is why
 * this exists. The failure it guards is the fog volume being sampled without
 * trilinear filtering, or its resolution silently dropping: both show up as
 * visible banding in the light shafts, and neither produces a console error,
 * so the scene smoketest alone would call the frame a pass.
 *
 * It asserts on shape, not on an exact image. A reference-image diff would
 * have to be regenerated for every legitimate lighting change and would tell
 * you nothing about *why* it moved; what actually distinguishes the bug is
 * that smooth gradients become flat plateaus separated by steps. So the test
 * measures two independent properties of the lit fog region:
 *
 *   p99 |laplacian|  - the strength of the sharpest edges. Banding introduces
 *                      hard steps the smooth render does not have.
 *   flat fraction    - the share of horizontally adjacent pixel pairs that are
 *                      effectively identical. Blocks are plateaus, so this
 *                      jumps when the gradient is quantized.
 *
 * Both are measured at a canonical 960x540, so a HiDPI or resized canvas does
 * not move the numbers (the flat fraction in particular is resolution
 * sensitive -- finer gradients quantize to more equal neighbors).
 *
 * Calibration, measured on this exact scene (see self_test_fog_visual.mjs,
 * which re-checks these against committed images so a threshold cannot be
 * edited into uselessness):
 *
 *                       good   blocky64  blocky32  blocky16
 *   p99 |laplacian|      3.19     26.07     40.07     50.93
 *   flat fraction       48.9%     93.7%     96.8%     98.5%
 *
 * blocky64 is the important control: it is the engine's default 64-wide fog
 * volume sampled with no filtering at all. The thresholds sit between good and
 * blocky64 with roughly 3x margin on the passing side.
 *
 * SKIPS, rather than fails, when the adapter resolved to Forward Mobile:
 * `render_forward_mobile.cpp` contains no volumetric fog at all, so there is
 * nothing to measure there. A software adapter reports fewer than 48 textures
 * per shader stage and `RendererCompositorRD::initialize()` silently falls
 * back -- which is exactly how the 4.8 port shipped a black screen past a
 * green suite (Task 15.5). Set WEBGPU_REAL_GPU=1 to get a real adapter.
 *
 * Usage:
 *   node test_fog_visual.mjs [--browser chrome|firefox|all]
 */

import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath, pathToFileURL } from 'url';
import pw from 'playwright';
import { decodePng } from '../screenshot_comparison/png.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SCENE_DIR = join(__dirname, 'exports', 'volumetric_fog');

const MIME = {
  '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json',
  '.svg': 'image/svg+xml', '.ico': 'image/x-icon',
};

// The size every metric is computed at, matching the fixture's viewport.
const DESIGN_W = 960, DESIGN_H = 540;

// Luminance floor that separates lit fog from the near-black background. The
// scene's background is Color(0.02, 0.02, 0.03) and the shafts run well above
// 70, so this sits far from both.
const FOG_LUMA_MIN = 12;

// Thresholds. See the calibration table above; each is roughly midway on a log
// scale between the good render and the mildest simulated regression.
const MAX_P99_LAPLACIAN = 10.0;
const MAX_FLAT_FRACTION = 0.70;
// Liveness guards. Without these a black or fog-less frame would trivially
// satisfy "no hard edges, no plateaus" and pass for entirely the wrong reason
// -- the same trap test_font_visual.mjs's backdrop check exists to close.
const MIN_FOG_AREA = 0.15;
const MIN_FOG_MEAN_LUMA = 40;

const luma = (r, g, b) => r * 0.2126 + g * 0.7152 + b * 0.0722;

/**
 * Area-average the decoded frame down to DESIGN_W x DESIGN_H luminance.
 * Downscaling only: an upscale would invent detail and silently shift every
 * threshold, so a smaller canvas is reported as an error instead.
 */
function toDesignLuma(png) {
  if (png.width < DESIGN_W || png.height < DESIGN_H) {
    return { error: `canvas ${png.width}x${png.height} is smaller than the ${DESIGN_W}x${DESIGN_H} the thresholds are calibrated for` };
  }
  const src = new Float64Array(png.width * png.height);
  for (let i = 0, p = 0; i < src.length; i++, p += 4) {
    src[i] = luma(png.data[p], png.data[p + 1], png.data[p + 2]);
  }
  if (png.width === DESIGN_W && png.height === DESIGN_H) {
    return { luma: src, w: DESIGN_W, h: DESIGN_H };
  }
  const out = new Float64Array(DESIGN_W * DESIGN_H);
  const sx = png.width / DESIGN_W, sy = png.height / DESIGN_H;
  for (let y = 0; y < DESIGN_H; y++) {
    const y0 = Math.floor(y * sy), y1 = Math.max(y0 + 1, Math.floor((y + 1) * sy));
    for (let x = 0; x < DESIGN_W; x++) {
      const x0 = Math.floor(x * sx), x1 = Math.max(x0 + 1, Math.floor((x + 1) * sx));
      let sum = 0, n = 0;
      for (let yy = y0; yy < y1; yy++) {
        for (let xx = x0; xx < x1; xx++) { sum += src[yy * png.width + xx]; n++; }
      }
      out[y * DESIGN_W + x] = sum / n;
    }
  }
  return { luma: out, w: DESIGN_W, h: DESIGN_H };
}

/**
 * The assertion proper. Exported so self_test_fog_visual.mjs can exercise it
 * against still images with no browser, export or GPU.
 */
export function assertFogSmoothness(png) {
  const down = toDesignLuma(png);
  if (down.error) return { failures: [down.error], metrics: null };
  const { luma: L, w, h } = down;
  const at = (x, y) => L[y * w + x];
  const isFog = (x, y) => at(x, y) > FOG_LUMA_MIN;

  let fogCount = 0, fogSum = 0;
  for (let i = 0; i < L.length; i++) {
    if (L[i] > FOG_LUMA_MIN) { fogCount++; fogSum += L[i]; }
  }
  const area = fogCount / L.length;
  const meanLuma = fogCount ? fogSum / fogCount : 0;

  const laps = [];
  for (let y = 1; y < h - 1; y++) {
    for (let x = 1; x < w - 1; x++) {
      if (!isFog(x, y)) continue;
      laps.push(Math.abs(4 * at(x, y) - at(x - 1, y) - at(x + 1, y) - at(x, y - 1) - at(x, y + 1)));
    }
  }
  let flat = 0, pairs = 0;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w - 1; x++) {
      if (!isFog(x, y)) continue;
      pairs++;
      if (Math.abs(at(x + 1, y) - at(x, y)) < 0.5) flat++;
    }
  }
  laps.sort((a, b) => a - b);
  const p99 = laps.length ? laps[Math.min(laps.length - 1, Math.floor(laps.length * 0.99))] : 0;
  const flatFraction = pairs ? flat / pairs : 0;
  const metrics = { area, meanLuma, p99Laplacian: p99, flatFraction };

  const failures = [];
  if (area < MIN_FOG_AREA) {
    failures.push(`lit fog covers ${(area * 100).toFixed(1)}% of the frame (< ${MIN_FOG_AREA * 100}%) -- the fog may not have rendered at all`);
  }
  if (meanLuma < MIN_FOG_MEAN_LUMA) {
    failures.push(`lit fog mean luminance ${meanLuma.toFixed(1)} (< ${MIN_FOG_MEAN_LUMA}) -- the fog may not have rendered at all`);
  }
  // Only meaningful once the guards above confirm there is fog to measure.
  if (failures.length === 0) {
    if (p99 > MAX_P99_LAPLACIAN) {
      failures.push(`p99 |laplacian| ${p99.toFixed(2)} (> ${MAX_P99_LAPLACIAN}) -- fog is banding; froxel volume may be sampled unfiltered`);
    }
    if (flatFraction > MAX_FLAT_FRACTION) {
      failures.push(`flat fraction ${(flatFraction * 100).toFixed(1)}% (> ${MAX_FLAT_FRACTION * 100}%) -- fog gradient is quantized into plateaus`);
    }
  }
  return { failures, metrics };
}

function startServer() {
  return new Promise((resolve, reject) => {
    const server = createServer((req, res) => {
      const p = join(SCENE_DIR, req.url === '/' ? 'index.html' : req.url.split('?')[0]);
      if (!existsSync(p)) { res.writeHead(404); return res.end('Not found'); }
      res.setHeader('Content-Type', MIME[extname(p)] || 'application/octet-stream');
      res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
      res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      res.end(readFileSync(p));
    });
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      resolve({ server, port, url: `http://127.0.0.1:${port}` });
    });
  });
}

/** Mirrors test_font_visual.mjs / run_scenes.mjs so this runs wherever they do. */
async function launchBrowser(browserName) {
  const isCI = !!process.env.CI;
  if (browserName === 'chrome') {
    if (process.env.WEBGPU_REAL_GPU) {
      return pw.chromium.launch({
        headless: true,
        args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan',
          '--use-angle=vulkan', '--use-vulkan=native', '--ignore-gpu-blocklist', '--no-sandbox'],
      });
    }
    return pw.chromium.launch({
      headless: true,
      args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer',
        '--use-angle=swiftshader', '--enable-gpu'],
    });
  }
  if (browserName === 'firefox') {
    return pw.firefox.launch({
      headless: isCI,
      firefoxUserPrefs: { 'dom.webgpu.enabled': true, 'gfx.webgpu.force-enabled': true },
    });
  }
  return null;
}

/**
 * Environments this test cannot run in, checked before launching anything so
 * the reason is stated plainly rather than surfacing as a confusing failure.
 *
 * Both were measured, not assumed:
 *
 * - The harness's default Chrome launch forces `--use-angle=swiftshader`.
 *   Under it this scene renders the lit floor pools and the boxes but no
 *   shafts at all -- volumetric fog is simply absent, identically at 8s, 12s
 *   and 20s, so it is not slow convergence. There is nothing to measure.
 * - Headless Firefox cannot initialize a compositor here ("RenderCompositorSWGL
 *   failed mapping default framebuffer", TASKS.md Task 9.5); its screenshots
 *   come back fully black including the 2D UI, while the same build headed
 *   renders correctly. That is what makes the font-colour tier fail in CI, and
 *   this test would fail the same way for the same non-reason.
 *
 * A fog-less frame is otherwise a genuine failure -- that is what the liveness
 * guard is for -- so these are narrow, explicit exemptions rather than a
 * blanket "no fog means skip".
 */
function unsupportedReason(browserName) {
  if (browserName === 'chrome' && !process.env.WEBGPU_REAL_GPU) {
    return 'the default Chrome launch forces the swiftshader adapter, which renders no volumetric fog at all -- set WEBGPU_REAL_GPU=1 for a real adapter';
  }
  if (browserName === 'firefox' && process.env.CI) {
    return 'headless Firefox cannot composite in this environment (canvas reads back black, 2D UI included)';
  }
  return null;
}

async function testBrowser(browserName) {
  if (!existsSync(SCENE_DIR)) {
    console.log(`  ${browserName}: SKIP -- no export at ${SCENE_DIR} (run: node run_scenes.mjs --export --scene volumetric_fog)`);
    return 'SKIP';
  }
  const unsupported = unsupportedReason(browserName);
  if (unsupported) {
    console.log(`  ${browserName}: SKIP -- ${unsupported}`);
    return 'SKIP';
  }

  const { server, url } = await startServer();
  let browser;
  try {
    browser = await launchBrowser(browserName);
  } catch (e) {
    console.log(`  ${browserName}: SKIP -- could not launch (${e.message})`);
    server.close();
    return 'SKIP';
  }
  if (!browser) {
    console.log(`  ${browserName}: SKIP -- unsupported browser`);
    server.close();
    return 'SKIP';
  }

  const page = await browser.newPage({ viewport: { width: DESIGN_W, height: DESIGN_H } });
  let banner = null;
  page.on('console', (msg) => {
    const t = msg.text();
    // e.g. "WebGPU 1.0 - Forward+ - Using Device #0: ..."
    if (!banner && /WebGPU .* - Forward/.test(t)) banner = t;
  });

  let result = 'FAIL';
  try {
    await page.goto(url);
    const deadline = Date.now() + 60000;
    while (!banner && Date.now() < deadline) await new Promise((r) => setTimeout(r, 300));

    if (!banner) {
      console.log(`  ${browserName}: FAIL -- engine never reported a renderer within 60s`);
    } else if (!banner.includes('Forward+')) {
      // Not a failure of the fog: there is simply no volumetric fog to measure.
      console.log(`  ${browserName}: SKIP -- adapter resolved to Forward Mobile, which has no volumetric fog`);
      console.log('            (software adapters report < 48 textures/stage and fall back; set WEBGPU_REAL_GPU=1)');
      result = 'SKIP';
    } else {
      // The froxel volume accumulates over several frames; sample once it has settled.
      await page.waitForTimeout(8000);
      const canvas = await page.locator('canvas').first();
      const png = decodePng(await canvas.screenshot());
      const { failures, metrics } = assertFogSmoothness(png);
      if (metrics) {
        console.log(`  ${browserName}: canvas ${png.width}x${png.height}  ` +
          `fog ${(metrics.area * 100).toFixed(1)}% luma ${metrics.meanLuma.toFixed(1)}  ` +
          `p99|lap| ${metrics.p99Laplacian.toFixed(2)}  flat ${(metrics.flatFraction * 100).toFixed(1)}%`);
      }
      if (failures.length === 0) {
        result = 'PASS';
      } else {
        for (const f of failures) console.log(`      ${f}`);
      }
      console.log(`  ${browserName}: ${result}`);
    }
  } finally {
    await browser.close();
    server.close();
  }
  return result;
}

// Guarded so assertFogSmoothness can be imported and exercised against still
// images without launching a browser -- see self_test_fog_visual.mjs.
if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const args = process.argv.slice(2);
  const browserArg = args.includes('--browser') ? args[args.indexOf('--browser') + 1] : 'chrome';
  const browsers = browserArg === 'all' ? ['chrome', 'firefox'] : [browserArg];

  console.log('Volumetric Fog Smoothness Test\n');

  let anyFail = false, anyRan = false;
  for (const b of browsers) {
    const r = await testBrowser(b);
    if (r === 'FAIL') anyFail = true;
    if (r !== 'SKIP') anyRan = true;
  }

  // Exit 2 when nothing actually ran, so local_ci.sh reports SKIP rather than
  // PASS. A tier that silently passes by not running is worse than no tier.
  if (anyFail) {
    console.log('\nRESULT: FAIL');
    process.exit(1);
  }
  if (!anyRan) {
    console.log('\nRESULT: SKIP');
    process.exit(2);
  }
  console.log('\nRESULT: PASS');
  process.exit(0);
}
