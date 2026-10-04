/**
 * Font rendering colour-correctness test.
 *
 * Guards a real regression: both text servers used to decide whether a glyph
 * carries its own colour -- and so must not be tinted by the text colour -- by
 * checking whether the glyph atlas was RGBA8. On WebGPU monochrome atlases ARE
 * RGBA8 (there is no texture component swizzle to broadcast luminance), so that
 * test matched every ordinary glyph, the modulate was discarded, and ALL text
 * rendered white. See webgpu_notes/TASKS.md, "Task 35 -- follow-up".
 *
 * The important property of this test is that it renders text which is NOT
 * white. Task 35's original verification was "text renders correctly" checked
 * against white text -- and the bug makes text white, so that check could not
 * distinguish the broken state from the working one. Do not weaken the colors
 * here to something closer to white.
 *
 * Asserting is done on a screenshot of the canvas rather than inside GDScript
 * because RenderingDeviceDriverWebGPU::texture_get_data() is a one-shot cache:
 * a caller that calls texture_2d_get() once can never get data on WebGPU
 * (webgpu_notes/HANDOFF.md 4.6). The screenshot is also the more faithful test
 * -- it checks what actually reached the screen.
 *
 * Usage:
 *   node test_font_visual.mjs [--browser chrome|firefox|all]
 */

import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath, pathToFileURL } from 'url';
import pw from 'playwright';
import { decodePng } from '../screenshot_comparison/png.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SCENE_DIR = join(__dirname, 'exports', 'font_rendering');

const MIME = {
  '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json',
  '.svg': 'image/svg+xml', '.ico': 'image/x-icon',
};

// Viewport coordinates, matching BAND_FILL / BAND_OUTLINE in font_check.gd.
// Scaled to the real canvas size at assert time, so a HiDPI or resized canvas
// is handled without changing anything here.
const DESIGN_W = 1280, DESIGN_H = 720;
const REGIONS = {
  fill: { x0: 40, y0: 80, x1: 1240, y1: 280 },
  outline: { x0: 40, y0: 360, x1: 1240, y1: 600 },
};

// Measured on a correct native render vs. one with the bug's effect simulated
// (both labels forced to white, which is exactly what the bug does):
//
//            band      good      bug
//   fill     red       19.0%     0.0%
//   fill     white      0.0%    18.1%
//   outline  black     12.3%     0.0%
//
// The thresholds sit roughly midway on a log scale between those, so there is
// several times the margin on the passing side and the failing side is zero,
// not merely low. Fractions of band area, so they do not depend on resolution.
const MIN_FILL_RED = 0.05;
const MAX_FILL_WHITE = 0.03;
const MIN_OUTLINE_BLACK = 0.04;
// Both bands sit on a 0.5 gray backdrop (~72-80% of each band when correct).
// Without this, a blank or black canvas would satisfy "almost no white pixels"
// and the fill band would pass for the wrong reason.
const MIN_BACKDROP_GREY = 0.20;

const isRed = (r, g, b) => r > 140 && g < 90 && b < 90;
const isWhite = (r, g, b) => r > 230 && g > 230 && b > 230;
const isBlack = (r, g, b) => r < 60 && g < 60 && b < 60;
const isGrey = (r, g, b) =>
  Math.abs(r - 128) < 25 && Math.abs(g - 128) < 25 && Math.abs(b - 128) < 25;

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
    server.listen(0, () => resolve({ server, url: `http://127.0.0.1:${server.address().port}` }));
  });
}

/** Count how many pixels in a region satisfy each predicate, as area fractions. */
export function measure(png, region) {
  const sx = png.width / DESIGN_W, sy = png.height / DESIGN_H;
  const x0 = Math.round(region.x0 * sx), x1 = Math.round(region.x1 * sx);
  const y0 = Math.round(region.y0 * sy), y1 = Math.round(region.y1 * sy);
  let red = 0, white = 0, black = 0, gray = 0, total = 0;
  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      const i = (y * png.width + x) * 4;
      const r = png.data[i], g = png.data[i + 1], b = png.data[i + 2];
      if (isRed(r, g, b)) red++;
      if (isWhite(r, g, b)) white++;
      if (isBlack(r, g, b)) black++;
      if (isGrey(r, g, b)) gray++;
      total++;
    }
  }
  if (total === 0) return null;
  return { red: red / total, white: white / total, black: black / total, gray: gray / total, total };
}

export function assertBands(png) {
  const failures = [];
  const fill = measure(png, REGIONS.fill);
  const outline = measure(png, REGIONS.outline);
  if (!fill || !outline) return { failures: ['canvas too small to contain either band'], fill, outline };

  const pct = (v) => `${(v * 100).toFixed(1)}%`;

  if (fill.gray < MIN_BACKDROP_GREY) {
    failures.push(`fill band backdrop missing (gray ${pct(fill.gray)} < ${pct(MIN_BACKDROP_GREY)}) -- scene may not have rendered`);
  }
  if (outline.gray < MIN_BACKDROP_GREY) {
    failures.push(`outline band backdrop missing (gray ${pct(outline.gray)} < ${pct(MIN_BACKDROP_GREY)}) -- scene may not have rendered`);
  }
  if (fill.red < MIN_FILL_RED) {
    failures.push(`red text not rendered red (red ${pct(fill.red)} < ${pct(MIN_FILL_RED)}) -- glyph modulate is being dropped`);
  }
  if (fill.white > MAX_FILL_WHITE) {
    failures.push(`red text rendered white (white ${pct(fill.white)} > ${pct(MAX_FILL_WHITE)}) -- glyph modulate is being dropped`);
  }
  if (outline.black < MIN_OUTLINE_BLACK) {
    failures.push(`dark outline not rendered dark (black ${pct(outline.black)} < ${pct(MIN_OUTLINE_BLACK)}) -- outline modulate is being dropped`);
  }
  return { failures, fill, outline };
}

/**
 * Mirrors run_scenes.mjs's launch logic so this test runs wherever the scene
 * smoketest does. Notably `headless: true` under CI, so the workflow needs no
 * xvfb wrapper; and WEBGPU_REAL_GPU=1 to put a dev machine's bundled Chromium
 * on the real Vulkan adapter rather than swiftshader, which reports a
 * materially different set of capabilities (TASKS.md Task 9.5).
 */
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

async function testBrowser(browserName) {
  if (!existsSync(SCENE_DIR)) {
    console.log(`  ${browserName}: SKIP -- no export at ${SCENE_DIR} (run: node run_scenes.mjs --export-only)`);
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
  let ready = false;
  page.on('console', (msg) => { if (msg.text().includes('[FontCheck] READY')) ready = true; });

  let result = 'FAIL';
  try {
    await page.goto(url);
    const deadline = Date.now() + 60000;
    while (!ready && Date.now() < deadline) await new Promise((r) => setTimeout(r, 300));

    if (!ready) {
      console.log(`  ${browserName}: FAIL -- no [FontCheck] READY within 60s`);
    } else {
      const canvas = await page.locator('canvas').first();
      const png = decodePng(await canvas.screenshot());
      const { failures, fill, outline } = assertBands(png);
      const pct = (v) => `${(v * 100).toFixed(1)}%`;
      console.log(`  ${browserName}: canvas ${png.width}x${png.height}  ` +
        `fill[red ${pct(fill.red)} white ${pct(fill.white)}]  outline[black ${pct(outline.black)}]`);
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

// Guarded so the assertion helpers above can be imported and exercised against
// still images without launching a browser -- see self_test_font_visual.mjs.
if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const args = process.argv.slice(2);
  const browserArg = args.includes('--browser') ? args[args.indexOf('--browser') + 1] : 'chrome';
  const browsers = browserArg === 'all' ? ['chrome', 'firefox'] : [browserArg];

  console.log('Font Rendering Colour-Correctness Test\n');

  let anyFail = false;
  for (const b of browsers) {
    if ((await testBrowser(b)) === 'FAIL') anyFail = true;
  }

  console.log(anyFail ? '\nRESULT: FAIL' : '\nRESULT: PASS');
  process.exit(anyFail ? 1 : 0);
}
