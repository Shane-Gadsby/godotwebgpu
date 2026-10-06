/**
 * Forward+ feature matrix for the WebGPU port.
 *
 * For every feature in features.mjs: render the fixture with it off, render it
 * with it on, and assert the frame actually changed. That is a deliberately
 * weak assertion about correctness and a strong one about existence, because
 * the failure this port keeps producing is a feature silently doing nothing.
 * SSAO and SDFGI both shipped broken through a fully green suite in the 4.8
 * port (TASKS.md Task 15.5) precisely because nothing compared a frame against
 * the same frame without the effect.
 *
 * It also fails if either render logs a WebGPU/engine error, so a feature that
 * "works" by spraying validation errors is not counted as working.
 *
 * Requires a real GPU. Volumetric fog, SDFGI, SSAO and SSIL do not render at
 * all under a software adapter, and the whole point of a Forward+ matrix is
 * that Forward+ is actually running -- a software adapter reports fewer than 48
 * textures per shader stage and the engine silently falls back to Forward
 * Mobile, where a third of this matrix does not exist. The run SKIPs with that
 * reason rather than reporting a wall of false failures.
 *
 * Usage:
 *   WEBGPU_REAL_GPU=1 node run_forward_plus.mjs [--browser chrome|firefox]
 *                                               [--feature <id>] [--list]
 *                                               [--json <path>]
 */

import { createServer } from 'http';
import { readFileSync, existsSync, writeFileSync } from 'fs';
import { join, extname, dirname } from 'path';
import { fileURLToPath } from 'url';
import pw from 'playwright';
import { decodePng } from '../screenshot_comparison/png.mjs';
import { FEATURES, UNCOVERED, CATEGORIES, byId } from './features.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SCENE_DIR = join(__dirname, 'export');

const MIME = {
  '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json',
  '.svg': 'image/svg+xml', '.ico': 'image/x-icon',
};

const VIEW_W = 640, VIEW_H = 360;
// Errors that are expected noise rather than a failure. Kept explicit and
// narrow: a broad filter here would hide exactly what this suite is for.
const BENIGN_ERROR = /float32-blendable|AudioContext|_check_capabilities|Source map/i;

export function meanAbsDiff(a, b) {
  if (a.width !== b.width || a.height !== b.height) return null;
  let sum = 0;
  const n = a.width * a.height;
  for (let i = 0, p = 0; i < n; i++, p += 4) {
    sum += Math.abs(a.data[p] - b.data[p]) +
      Math.abs(a.data[p + 1] - b.data[p + 1]) +
      Math.abs(a.data[p + 2] - b.data[p + 2]);
  }
  return sum / (n * 3);
}

/** Decide one feature's result from its measured delta and any errors seen. */
export function judge(feature, delta, errors) {
  if (errors.length) {
    return { ok: false, why: `${errors.length} error(s): ${errors[0].slice(0, 120)}` };
  }
  if (delta === null) return { ok: false, why: 'frame size changed between off and on' };
  if (delta < feature.minDelta) {
    return { ok: false, why: `delta ${delta.toFixed(3)} < required ${feature.minDelta} -- feature appears to do nothing` };
  }
  return { ok: true, why: `delta ${delta.toFixed(3)}` };
}

function startServer() {
  return new Promise((resolve, reject) => {
    const server = createServer((req, res) => {
      const p = join(SCENE_DIR, req.url === '/' ? 'index.html' : req.url.split('?')[0].split('#')[0]);
      if (!existsSync(p)) { res.writeHead(404); return res.end('Not found'); }
      res.setHeader('Content-Type', MIME[extname(p)] || 'application/octet-stream');
      res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
      res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      res.end(readFileSync(p));
    });
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => resolve({ server, url: `http://127.0.0.1:${server.address().port}` }));
  });
}

async function launchBrowser(name) {
  if (name === 'chrome') {
    return pw.chromium.launch({
      headless: true,
      args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan',
        '--use-angle=vulkan', '--use-vulkan=native', '--ignore-gpu-blocklist', '--no-sandbox'],
    });
  }
  if (name === 'firefox') {
    return pw.firefox.launch({
      headless: !!process.env.CI,
      firefoxUserPrefs: { 'dom.webgpu.enabled': true, 'gfx.webgpu.force-enabled': true },
    });
  }
  return null;
}

function unsupportedReason(browserName) {
  if (browserName === 'chrome' && !process.env.WEBGPU_REAL_GPU) {
    return 'needs a real GPU adapter (set WEBGPU_REAL_GPU=1); under swiftshader several Forward+ effects do not render at all';
  }
  if (browserName === 'firefox' && process.env.CI) {
    return 'headless Firefox cannot composite in this environment (canvas reads back black)';
  }
  return null;
}

async function main() {
  const args = process.argv.slice(2);
  const arg = (n, d) => (args.includes(n) ? args[args.indexOf(n) + 1] : d);
  const browserName = arg('--browser', 'chrome');
  const only = arg('--feature', null);
  const jsonOut = arg('--json', null);

  if (args.includes('--list')) {
    for (const c of Object.keys(CATEGORIES)) {
      console.log(`\n${CATEGORIES[c]}`);
      for (const f of FEATURES.filter((x) => x.cat === c)) console.log(`  ${f.id.padEnd(28)} ${f.label}`);
    }
    console.log(`\nNot covered (${UNCOVERED.length}):`);
    for (const u of UNCOVERED) console.log(`  ${u.id.padEnd(28)} ${u.why}`);
    console.log(`\n${FEATURES.length} covered, ${UNCOVERED.length} documented as uncovered.`);
    process.exit(0);
  }

  console.log(`Forward+ Feature Matrix (WebGPU) -- ${FEATURES.length} features\n`);

  if (!existsSync(SCENE_DIR)) {
    console.log(`  SKIP -- no export at ${SCENE_DIR} (run ./export.sh)`);
    process.exit(2);
  }
  const unsupported = unsupportedReason(browserName);
  if (unsupported) {
    console.log(`  SKIP -- ${unsupported}`);
    process.exit(2);
  }

  const { server, url } = await startServer();
  let browser;
  try {
    browser = await launchBrowser(browserName);
  } catch (e) {
    console.log(`  SKIP -- could not launch ${browserName} (${e.message})`);
    server.close();
    process.exit(2);
  }
  if (!browser) {
    console.log(`  SKIP -- unsupported browser ${browserName}`);
    server.close();
    process.exit(2);
  }

  const page = await browser.newPage({ viewport: { width: VIEW_W, height: VIEW_H } });
  let banner = null;
  let declaredFeatures = null;
  let ready = null;
  let unknown = null;
  let errors = [];
  page.on('console', (m) => {
    const t = m.text();
    if (!banner && /WebGPU .* - Forward/.test(t)) banner = t;
    if (t.startsWith('[FP] FEATURES ')) declaredFeatures = t.slice('[FP] FEATURES '.length).trim().split(',');
    if (t.startsWith('[FP] READY ')) ready = t.slice('[FP] READY '.length).trim();
    if (t.startsWith('[FP] UNKNOWN ')) unknown = t.slice('[FP] UNKNOWN '.length).trim();
    if (m.type() === 'error' && !BENIGN_ERROR.test(t)) errors.push(t);
  });
  page.on('pageerror', (e) => { if (!BENIGN_ERROR.test(e.message)) errors.push(e.message); });

  const results = [];
  let exitCode = 0;
  try {
    await page.goto(url);
    const bootDeadline = Date.now() + 90000;
    while (!banner && Date.now() < bootDeadline) await new Promise((r) => setTimeout(r, 200));
    if (!banner) {
      console.log('  FAIL -- engine never reported a renderer within 90s');
      process.exitCode = 1;
      return;
    }
    console.log(`  ${banner.trim()}\n`);
    if (!banner.includes('Forward+')) {
      console.log('  SKIP -- adapter resolved to Forward Mobile; a third of this matrix does not exist there');
      await browser.close();
      server.close();
      process.exit(2);
    }
    while (!declaredFeatures && Date.now() < bootDeadline) await new Promise((r) => setTimeout(r, 200));

    // Drift check: the fixture's own list and this test's list must agree, in
    // both directions. This is what makes "a new feature needs a test" an
    // enforced rule rather than a request in a README.
    if (declaredFeatures) {
      const mine = new Set(FEATURES.map((f) => f.id));
      const theirs = new Set(declaredFeatures);
      const missingHere = [...theirs].filter((x) => !mine.has(x));
      const missingThere = [...mine].filter((x) => !theirs.has(x));
      if (missingHere.length || missingThere.length) {
        for (const m of missingHere) console.log(`  DRIFT  fixture drives "${m}" but features.mjs has no entry -- add one`);
        for (const m of missingThere) console.log(`  DRIFT  features.mjs lists "${m}" but fp_scene.gd cannot drive it -- add an _apply() arm`);
        console.log('');
        exitCode = 1;
      }
    } else {
      console.log('  WARN  fixture never published its feature list; drift check skipped\n');
    }

    const wanted = only ? FEATURES.filter((f) => f.id === only) : FEATURES;
    if (only && wanted.length === 0) {
      console.log(`  FAIL -- unknown feature "${only}" (use --list)`);
      process.exitCode = 1;
      return;
    }

    const shoot = async (id, on) => {
      ready = null; unknown = null;
      await page.evaluate(([i, o]) => { window.location.hash = `fp=${i}:${o}`; }, [id, on ? 1 : 0]);
      const deadline = Date.now() + 45000;
      while (!ready && !unknown && Date.now() < deadline) await new Promise((r) => setTimeout(r, 60));
      if (unknown) return { unknown: true };
      if (!ready) return { timeout: true };
      const canvas = await page.locator('canvas').first();
      return { png: decodePng(await canvas.screenshot()) };
    };

    let lastCat = null;
    for (const f of wanted) {
      if (f.cat !== lastCat) { console.log(`\n  ${CATEGORIES[f.cat]}`); lastCat = f.cat; }
      errors = [];
      const off = await shoot(f.id, false);
      const on = await shoot(f.id, true);
      let verdict;
      if (off.unknown || on.unknown) {
        verdict = { ok: false, why: 'fixture does not implement this feature' };
      } else if (off.timeout || on.timeout) {
        verdict = { ok: false, why: 'fixture never signaled READY within 45s' };
      } else {
        verdict = judge(f, meanAbsDiff(off.png, on.png), errors);
      }
      results.push({ id: f.id, cat: f.cat, label: f.label, ok: verdict.ok, why: verdict.why });
      console.log(`    ${verdict.ok ? 'PASS' : 'FAIL'}  ${f.id.padEnd(28)} ${verdict.why}`);
      if (!verdict.ok) exitCode = 1;

      // A feature that fails with driver errors usually keeps failing every
      // frame afterwards -- a broken texture leaves its uniform sets invalid --
      // and those errors then land during the NEXT feature's window and get
      // blamed on it. (That is not hypothetical: VoxelGI's R8G8_Uint failure
      // was initially reported against proximity_fade, which is entirely
      // innocent.) Reload to get a clean engine rather than propagate it.
      if (!verdict.ok && errors.length) {
        banner = null;
        await page.goto(url);
        const d = Date.now() + 90000;
        while (!banner && Date.now() < d) await new Promise((r) => setTimeout(r, 200));
      }
    }
  } finally {
    await browser.close();
    server.close();
  }

  const passed = results.filter((r) => r.ok).length;
  console.log(`\n  ${passed}/${results.length} features pass`);
  if (UNCOVERED.length) {
    console.log(`  ${UNCOVERED.length} Forward+ features are documented as not covered (--list to see them)`);
  }
  if (jsonOut) {
    writeFileSync(jsonOut, JSON.stringify({ banner, results, uncovered: UNCOVERED }, null, 2));
    console.log(`  wrote ${jsonOut}`);
  }
  console.log(exitCode === 0 ? '\nRESULT: PASS' : '\nRESULT: FAIL');
  process.exit(exitCode);
}

if (process.argv[1] && import.meta.url.endsWith(process.argv[1].split('/').pop())) {
  await main();
}
