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
 * --parity adds the stronger check: every captured frame is compared against a
 * native Vulkan render of the same configuration, and a feature fails when the
 * two differ by more than its tolerance. The delta test proves a feature does
 * something; parity proves it does the same thing as native. Both sides run a
 * fixed timestep (--fixed-fps 60) and one long settle so every accumulating
 * effect has converged, and the fixture freezes the frame it reports READY on,
 * so the screenshot is that frame. The reference is rendered by running the
 * native editor on the fixture (--godot, or GODOT_EDITOR_BIN, or the repo's
 * bin/godot.linuxbsd.editor.x86_64) unless --reference names an existing one.
 * It needs a native Vulkan device and a display, as the web side needs a GPU.
 *
 * Usage:
 *   WEBGPU_REAL_GPU=1 node run_forward_plus.mjs [--browser chrome|firefox]
 *                                               [--feature <id>[,<id>...]] [--save-dir DIR] [--list] [--allow-stale-export]
 *                                               [--json <path>] [--dir <export dir>]
 *                                               [--parity [--reference DIR] [--godot BIN] [--settle N]]
 */

import { createServer } from 'http';
import { spawnSync } from 'child_process';
import { readFileSync, existsSync, writeFileSync, mkdirSync, mkdtempSync, statSync, rmSync } from 'fs';
import { tmpdir } from 'os';
import { join, extname, dirname } from 'path';
import { fileURLToPath } from 'url';
import pw from 'playwright';
import { decodePng } from '../screenshot_comparison/png.mjs';
import { FEATURES, UNCOVERED, CATEGORIES, PARITY_TOLERANCE, byId } from './features.mjs';
import { attachAudit } from '../scene_smoketest/webgpu_audit.mjs';
import { FIREFOX_WEBGPU_PREFS } from '../scene_smoketest/firefox_prefs.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
// --baked runs against export_baked/ (made by `BAKE=1 ./export.sh`) and additionally asserts the
// shader cache was actually used. The default export is unbaked (--headless skips the baker).
const BAKED = process.argv.includes('--baked');
// --dir <export> runs an export kept elsewhere (an A/B of two converters' exports, work task 11).
const DIR_ARG = process.argv.includes('--dir') ? process.argv[process.argv.indexOf('--dir') + 1] : null;
const SCENE_DIR = DIR_ARG || join(__dirname, BAKED ? 'export_baked' : 'export');
// The fixture builds materials in GDScript, so a few shaders legitimately translate at runtime. Measured 117 at boot
// once the work/21 arms added their own GDScript-built shaders (14 scene-shader materials x 8 groups, the trail
// ribbon's and skinned strip's among them, plus a fog shader, a trail process material and the compositor's compute
// shader); it was under 80 before. 133 once work task 26's dof_nan arm added its GDScript-built NaN material
// (2026-10-26). A broken bake translates the ~440 baked shaders too, far past this.
const TRANSLATED_CEILING = 150;

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

/** Parse a fixture render-info line ("objects=8 primitives=13238 draws=7") into numbers. */
export function parseInfo(text) {
  if (!text) return null;
  const out = {};
  for (const m of text.matchAll(/(\w+)=(\d+)/g)) out[m[1]] = Number(m[2]);
  return Object.keys(out).length ? out : null;
}

/**
 * A feature's infoDrop check: "on" must draw at least N fewer of each key than
 * "off". Returns null when the feature has none, else { ok, why }.
 */
export function judgeInfo(feature, infoOff, infoOn, side = '') {
  if (!feature.infoDrop) return null;
  if (!infoOff || !infoOn) return { ok: false, why: `no render info${side}` };
  const parts = [];
  for (const [k, n] of Object.entries(feature.infoDrop)) {
    const drop = (infoOff[k] ?? 0) - (infoOn[k] ?? 0);
    parts.push(`${k} ${infoOff[k]}->${infoOn[k]}`);
    if (!(drop >= n)) return { ok: false, why: `${k} ${infoOff[k]} -> ${infoOn[k]}${side}: dropped ${drop}, need >= ${n}` };
  }
  return { ok: true, why: parts.join(' ') + side };
}

/** Decide one feature's result from its measured delta and any errors seen. */
export function judge(feature, delta, errors, infoOff = null, infoOn = null) {
  if (errors.length) {
    return { ok: false, why: `${errors.length} error(s): ${errors[0].slice(0, 120)}` };
  }
  if (delta === null) return { ok: false, why: 'frame size changed between off and on' };
  if (feature.maxDelta !== undefined && delta > feature.maxDelta) {
    return { ok: false, why: `delta ${delta.toFixed(3)} > allowed ${feature.maxDelta} -- the frame must not change` };
  }
  if (delta < feature.minDelta) {
    return { ok: false, why: `delta ${delta.toFixed(3)} < required ${feature.minDelta} -- feature appears to do nothing` };
  }
  const info = judgeInfo(feature, infoOff, infoOn);
  if (info && !info.ok) return info;
  return { ok: true, why: `delta ${delta.toFixed(3)}${info ? '; ' + info.why : ''}` };
}

/**
 * Parity verdict: both captured frames must match the native reference within
 * the feature's tolerance. Diffs are mean absolute differences (0-255); null
 * means a missing reference or a size mismatch, which fails.
 */
export function judgeParity(feature, offDiff, onDiff) {
  const tol = feature.parityTol ?? PARITY_TOLERANCE;
  if (offDiff === null || onDiff === null) return { ok: false, why: 'no comparable native reference frame' };
  const worst = Math.max(offDiff, onDiff);
  if (worst > tol) {
    return { ok: false, why: `differs from native by ${worst.toFixed(3)} (off ${offDiff.toFixed(3)}, on ${onDiff.toFixed(3)}) > ${tol}` };
  }
  return { ok: true, why: `native diff off ${offDiff.toFixed(3)} on ${onDiff.toFixed(3)}` };
}

// The engine arguments of the export's index.html, with the fixed timestep the
// parity mode needs added, so the web side simulates exactly the native side's time.
export function withFixedFps(html) {
  return html.replace(/"args":\[(\s*\])?/, (m, empty) => (empty ? '"args":["--fixed-fps","60"]' : '"args":["--fixed-fps","60",'));
}

function startServer(parity) {
  return new Promise((resolve, reject) => {
    const server = createServer((req, res) => {
      const p = join(SCENE_DIR, req.url === '/' ? 'index.html' : req.url.split('?')[0].split('#')[0]);
      if (!existsSync(p)) { res.writeHead(404); return res.end('Not found'); }
      res.setHeader('Content-Type', MIME[extname(p)] || 'application/octet-stream');
      res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
      res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
      if (parity && p.endsWith('index.html')) {
        return res.end(withFixedFps(readFileSync(p, 'utf8')));
      }
      res.end(readFileSync(p));
    });
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => resolve({ server, url: `http://127.0.0.1:${server.address().port}` }));
  });
}

async function launchBrowser(name) {
  if (name === 'chrome') {
    // System Chrome with the user's own launcher flags (see scene_smoketest/run_scenes.mjs):
    // --use-angle=vulkan / --use-vulkan=native / --no-sandbox make it report "WebGL2 missing".
    return pw.chromium.launch({
      headless: !!process.env.CI,
      ...(process.env.CI ? {} : { executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] }),
      args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
    });
  }
  if (name === 'firefox') {
    return pw.firefox.launch({
      headless: !!process.env.CI,
      firefoxUserPrefs: FIREFOX_WEBGPU_PREFS,
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
  const saveDir = arg('--save-dir', null); // keep every captured frame, for comparing against the native reference
  const jsonOut = arg('--json', null);
  const parity = args.includes('--parity');
  const settle = parseInt(arg('--settle', '240'), 10);
  const echoConsole = args.includes('--console'); // print the page's whole console, for triage

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

  console.log(`Forward+ Feature Matrix (WebGPU) -- ${FEATURES.length} features${parity ? ', parity against native Vulkan' : ''}\n`);

  if (!existsSync(SCENE_DIR)) {
    console.log(`  SKIP -- no export at ${SCENE_DIR} (run ${BAKED ? 'BAKE=1 ' : ''}./export.sh)`);
    process.exit(2);
  }
  // A stale export renders yesterday's fixture and engine and still compares cleanly against a fresh native reference:
  // --baked once ran a three-hour-old export_baked/ after only the unbaked one had been refreshed, and failed FSR2 and
  // volumetric fog on fixes that were already merged. Refuse an export older than what it was made from.
  const exportPck = join(SCENE_DIR, 'index.pck');
  const sources = [
    process.env.GODOT_TEMPLATE_ZIP || join(__dirname, '..', '..', 'bin', 'godot.web.template_release.wasm32.nothreads.zip'),
    process.env.GODOT_EDITOR_BIN || join(__dirname, '..', '..', 'bin', 'godot.linuxbsd.editor.x86_64'),
    join(__dirname, 'godot', 'fp_check', 'fp_scene.gd'),
  ].filter((f) => existsSync(f));
  const newer = existsSync(exportPck) ? sources.filter((f) => statSync(f).mtimeMs > statSync(exportPck).mtimeMs) : sources;
  if (newer.length && !args.includes('--allow-stale-export')) {
    console.log(`  FAIL -- ${SCENE_DIR} is older than ${newer.join(', ')}; re-run ${BAKED ? 'BAKE=1 ' : ''}./export.sh (or pass --allow-stale-export)`);
    process.exit(1);
  }
  // Arms marked `needsRaycast` need a template built with the raycast module (Embree, the only occlusion culler).
  // Web templates leave it out unless built with module_raycast_enabled=yes (platform/web/detect.py), so such an arm is
  // KNOWN against a template without it and a normal arm against one with it. Embree's own strings tell which.
  const wasmPath = join(SCENE_DIR, 'index.wasm');
  const hasRaycast = existsSync(wasmPath) && readFileSync(wasmPath).includes('Embree Ray Tracing Kernels');
  console.log(`  template raycast module (occlusion culling): ${hasRaycast ? 'yes' : 'no'}`);
  for (const f of FEATURES) {
    if (f.needsRaycast && !hasRaycast) f.knownFail = f.needsRaycast;
  }
  const unsupported = unsupportedReason(browserName);
  if (unsupported) {
    console.log(`  SKIP -- ${unsupported}`);
    process.exit(2);
  }

  let referenceDir = arg('--reference', null);
  if (parity && !referenceDir) {
    // Render the native reference from the fixture's own source, with the same
    // fixed timestep and settle the web side is about to use.
    const godot = arg('--godot', process.env.GODOT_EDITOR_BIN || join(__dirname, '..', '..', 'bin', 'godot.linuxbsd.editor.x86_64'));
    if (!existsSync(godot)) {
      console.log(`  SKIP -- parity needs a native editor to render the reference (no ${godot}; pass --godot or --reference)`);
      process.exit(2);
    }
    referenceDir = mkdtempSync(join(tmpdir(), 'fp_reference_'));
    // A shader cache left in the fixture by another engine build can hand the native renderer stale variants: on
    // 2026-10-26 it failed five pipelines ("Mismatch fragment shader output mask") and moved native SDFGI 7 points
    // off a web frame that was right all along (TASKS.md §3 trap 68). The reference must come from this build alone.
    rmSync(join(__dirname, 'godot', 'fp_check', '.godot', 'shader_cache'), { recursive: true, force: true });
    const sweep = ['--path', join(__dirname, 'godot', 'fp_check'), '--rendering-driver', 'vulkan', '--resolution', `${VIEW_W}x${VIEW_H}`,
      '--fixed-fps', '60', '--', `--fp-matrix=${referenceDir}`, `--fp-settle=${settle}`];
    if (only) sweep.push(`--fp-only=${only}`);
    console.log(`  rendering the native reference into ${referenceDir} ...`);
    const r = spawnSync(godot, sweep, { encoding: 'utf8', timeout: 30 * 60 * 1000 });
    if (r.status !== 0 || !/\[FP\] NATIVE-DONE/.test(r.stdout || '')) {
      console.log(`  SKIP -- the native reference run did not complete (exit ${r.status}); it needs a Vulkan device and a display`);
      console.log((r.stdout || '').split('\n').slice(-8).join('\n'));
      process.exit(2);
    }
  }

  const { server, url } = await startServer(parity);
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
  await attachAudit(page, 'forward_plus');
  let banner = null;
  let declaredFeatures = null;
  let ready = null;
  let unknown = null;
  let info = null;
  let errors = [];
  let cacheMismatch = false;
  page.on('console', (m) => {
    const t = m.text();
    if (echoConsole) console.log(`    [${m.type()}] ${t}`);
    if (!banner && /WebGPU .* - Forward/.test(t)) banner = t;
    if (t.startsWith('[FP] FEATURES ')) declaredFeatures = t.slice('[FP] FEATURES '.length).trim().split(',');
    if (t.startsWith('[FP] INFO ')) info = parseInfo(t.slice('[FP] INFO '.length));
    if (t.startsWith('[FP] READY ')) ready = t.slice('[FP] READY '.length).trim();
    if (t.startsWith('[FP] UNKNOWN ')) unknown = t.slice('[FP] UNKNOWN '.length).trim();
    if (/none of it matches what this build asks for/.test(t)) cacheMismatch = true;
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
    if (BAKED) {
      // A baked export that loads fine but bakes nothing (dropped variants, changed container
      // format, mismatched version hash) would otherwise pass with the slow path hiding it.
      await new Promise((r) => setTimeout(r, 1500));
      const st = await page.evaluate(() => window.godotWebGPUShaderStats || null);
      console.log(`  shader stats: ${JSON.stringify(st)}\n`);
      if (!st || !(st.baked > 0) || st.translated > TRANSLATED_CEILING || cacheMismatch) {
        console.log(`  FAIL  baked export did not use its shader cache (want baked > 0, translated <= ${TRANSLATED_CEILING}, no mismatch warning; mismatch=${cacheMismatch})`);
        exitCode = 1;
      }
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

    // A parity-only arm's correct "on" frame equals its "off" frame, so the
    // delta test has nothing to measure on it.
    const onlyIds = only ? only.split(',') : null; // --feature a,b runs several
    const wanted = (onlyIds ? FEATURES.filter((f) => onlyIds.includes(f.id)) : FEATURES).filter((f) => parity || !f.parityOnly);
    if (only && wanted.length === 0) {
      console.log(`  FAIL -- unknown feature "${only}" (use --list)`);
      process.exitCode = 1;
      return;
    }

    const shoot = async (id, on) => {
      ready = null; unknown = null; info = null;
      await page.evaluate(([i, o, n]) => { window.location.hash = n ? `fp=${i}:${o}:${n}` : `fp=${i}:${o}`; }, [id, on ? 1 : 0, parity ? settle : 0]);
      // A parity settle (and sdfgi_camera_move's flight) runs well past 45 s on a slow device.
      const deadline = Date.now() + (parity ? 120000 : 45000);
      while (!ready && !unknown && Date.now() < deadline) await new Promise((r) => setTimeout(r, 60));
      if (unknown) return { unknown: true };
      if (!ready) return { timeout: true };
      const canvas = await page.locator('canvas').first();
      const buf = await canvas.screenshot();
      if (saveDir) { mkdirSync(saveDir, { recursive: true }); writeFileSync(`${saveDir}/${id}_${on ? 1 : 0}.png`, buf); }
      return { png: decodePng(buf), info };
    };

    let lastCat = null;
    for (const f of wanted) {
      if (f.cat !== lastCat) { console.log(`\n  ${CATEGORIES[f.cat]}`); lastCat = f.cat; }
      errors = [];
      const off = await shoot(f.id, false);
      const on = await shoot(f.id, true);
      let verdict;
      let nativeInfo = null;
      if (off.unknown || on.unknown) {
        verdict = { ok: false, why: 'fixture does not implement this feature' };
      } else if (off.timeout || on.timeout) {
        verdict = { ok: false, why: 'fixture never signaled READY within 45s' };
      } else {
        verdict = f.parityOnly ? { ok: !errors.length, why: errors.length ? `${errors.length} error(s): ${errors[0].slice(0, 120)}` : 'parity only' }
          : judge(f, meanAbsDiff(off.png, on.png), errors, off.info, on.info);
        if (parity && verdict.ok) {
          const ref = (o) => {
            const p = join(referenceDir, `${f.id}_${o}.png`);
            return existsSync(p) ? decodePng(readFileSync(p)) : null;
          };
          const rOff = ref(0), rOn = ref(1);
          const pv = judgeParity(f, rOff ? meanAbsDiff(off.png, rOff) : null, rOn ? meanAbsDiff(on.png, rOn) : null);
          verdict = { ok: pv.ok, why: `${verdict.why}; ${pv.why}` };
        }
        if (parity) {
          // The native side must show the same drop, or the fixture no longer exercises the feature.
          // Checked even when the web side failed, so a knownFail arm cannot hide a broken fixture.
          const refInfo = (o) => {
            const p = join(referenceDir, `${f.id}_${o}.info`);
            return existsSync(p) ? parseInfo(readFileSync(p, 'utf8')) : null;
          };
          nativeInfo = judgeInfo(f, refInfo(0), refInfo(1), ' (native)');
          if (nativeInfo) verdict = { ok: verdict.ok && nativeInfo.ok, why: `${verdict.why}; ${nativeInfo.why}` };
        }
      }
      // knownFail: a documented failure (features.mjs says why) reports KNOWN and does not fail the run, as long as the
      // native side still does what the arm needs. If it starts passing, that is reported and fails the run, so the
      // marker gets removed rather than outliving the bug.
      let status = verdict.ok ? 'PASS' : 'FAIL';
      if (f.knownFail) {
        if (verdict.ok) {
          status = 'XPASS';
          verdict = { ok: false, why: `${verdict.why} -- marked knownFail but passes; remove the marker` };
        } else if (!(nativeInfo && !nativeInfo.ok)) {
          status = 'KNOWN';
          verdict = { ...verdict, why: `${verdict.why} [known: ${f.knownFail.slice(0, 90)}]` };
        }
      }
      results.push({ id: f.id, cat: f.cat, label: f.label, ok: verdict.ok, known: status === 'KNOWN', why: verdict.why });
      console.log(`    ${status.padEnd(5)} ${f.id.padEnd(28)} ${verdict.why}`);
      if (status === 'FAIL' || status === 'XPASS') exitCode = 1;

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
  const known = results.filter((r) => r.known);
  console.log(`\n  ${passed}/${results.length} features pass${known.length ? `, ${known.length} known failure(s): ${known.map((r) => r.id).join(', ')}` : ''}`);
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
