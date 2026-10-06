/**
 * Self-test for test_font_visual.mjs's assertions.
 *
 * A visual test that cannot fail is worthless, and this one's whole reason for
 * existing is that the bug it guards was once "verified" by a check that could
 * not distinguish broken from working. So the assertions are themselves checked
 * against two committed reference images:
 *
 *   good.png - the scene rendered correctly
 *   bug.png  - the same scene with the bug's effect simulated (both labels
 *              forced white, which is exactly what dropping the modulate does)
 *
 * Needs no browser, no export and no GPU, so it runs in CI next to the other
 * standalone tiers and catches a threshold edited into uselessness.
 *
 * Usage: node self_test_font_visual.mjs
 */

import { readFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { decodePng } from '../screenshot_comparison/png.mjs';
import { assertBands } from './test_font_visual.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const FIXTURES = join(__dirname, 'fixtures', 'font_rendering');

let failed = 0;

function check(name, expectPass) {
  const png = decodePng(readFileSync(join(FIXTURES, name)));
  const { failures, fill, outline } = assertBands(png);
  const passed = failures.length === 0;
  const pct = (v) => `${(v * 100).toFixed(1)}%`;
  const ok = passed === expectPass;
  if (!ok) failed++;
  console.log(`  ${ok ? 'ok  ' : 'FAIL'}  ${name.padEnd(9)} ` +
    `expected ${expectPass ? 'PASS' : 'FAIL'}, got ${passed ? 'PASS' : 'FAIL'}  ` +
    `[fill red ${pct(fill.red)} white ${pct(fill.white)} | outline black ${pct(outline.black)}]`);
  if (!passed) for (const f of failures) console.log(`          - ${f}`);
}

console.log('Font visual assertion self-test\n');
check('good.png', true);   // correct render must pass
check('bug.png', false);   // simulated regression must fail

console.log(failed === 0 ? '\nRESULT: PASS' : '\nRESULT: FAIL');
process.exit(failed === 0 ? 0 : 1);
