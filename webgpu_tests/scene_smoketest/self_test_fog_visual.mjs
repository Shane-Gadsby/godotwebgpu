/**
 * Self-test for test_fog_visual.mjs's assertions.
 *
 * A visual test that cannot fail is worthless. The fog report this guards was
 * once closed as "not reproducible" on the strength of numbers agreeing, and
 * numbers from an insensitive measure agree whatever happens -- so the measure
 * itself is checked here, against committed images:
 *
 *   good.png     - the scene rendered correctly (real WebGPU capture)
 *   blocky64.png - the same frame resampled through a 64-wide grid with no
 *                  filtering: the engine's default fog volume width, sampled
 *                  the way the regression would. The near-threshold case.
 *   blocky16.png - the same at 16 wide; blatant.
 *   black.png    - an empty frame. Must fail on the liveness guard, not pass
 *                  for having no edges and no plateaus.
 *
 * Needs no browser, no export and no GPU, so it runs in CI next to the other
 * standalone tiers and catches a threshold edited into uselessness.
 *
 * Usage: node self_test_fog_visual.mjs
 */

import { readFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { decodePng } from '../screenshot_comparison/png.mjs';
import { assertFogSmoothness } from './test_fog_visual.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const FIXTURES = join(__dirname, 'fixtures', 'volumetric_fog');

let failed = 0;

function check(name, expectPass, expectReason) {
  const png = decodePng(readFileSync(join(FIXTURES, name)));
  const { failures, metrics } = assertFogSmoothness(png);
  const passed = failures.length === 0;
  let ok = passed === expectPass;
  // A rejection has to be for the right reason: a blocky frame failing the
  // liveness guard would "pass" this self-test while proving nothing about
  // whether banding is detected.
  if (!passed && expectReason && !failures.some((f) => f.includes(expectReason))) {
    ok = false;
  }
  if (!ok) failed++;
  const m = metrics
    ? `p99|lap| ${metrics.p99Laplacian.toFixed(2)}  flat ${(metrics.flatFraction * 100).toFixed(1)}%  ` +
      `fog ${(metrics.area * 100).toFixed(1)}% luma ${metrics.meanLuma.toFixed(1)}`
    : '(no metrics)';
  console.log(`  ${ok ? 'ok  ' : 'FAIL'}  ${name.padEnd(13)} ` +
    `expected ${expectPass ? 'PASS' : 'FAIL'}, got ${passed ? 'PASS' : 'FAIL'}  [${m}]`);
  if (!passed) for (const f of failures) console.log(`          - ${f}`);
}

console.log('Fog visual assertion self-test\n');
check('good.png', true);                            // correct render must pass
check('blocky64.png', false, 'banding');             // default-width unfiltered sampling must be caught
check('blocky16.png', false, 'banding');             // blatant case must be caught
check('black.png', false, 'may not have rendered');  // empty frame must fail loudly, not quietly pass

console.log(failed === 0 ? '\nRESULT: PASS' : '\nRESULT: FAIL');
process.exit(failed === 0 ? 0 : 1);
