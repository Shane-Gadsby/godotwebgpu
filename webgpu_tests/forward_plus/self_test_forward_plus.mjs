/**
 * Self-test for the Forward+ matrix's own logic.
 *
 * The matrix is only worth anything if it can fail. Its verdict comes from two
 * small pure functions -- meanAbsDiff() and judge() -- and this exercises both
 * against synthetic frames, with no browser, export or GPU, so it runs in CI
 * next to the other standalone tiers.
 *
 * It also checks the feature table itself: duplicate ids, a feature that is
 * both covered and listed as uncovered, an UNCOVERED entry with no reason, or a
 * threshold of zero (which would make that feature's test unfailable) are all
 * caught here rather than discovered when a regression slips through.
 *
 * Usage: node self_test_forward_plus.mjs
 */

import { meanAbsDiff, judge } from './run_forward_plus.mjs';
import { FEATURES, UNCOVERED, CATEGORIES } from './features.mjs';

let failed = 0;
function check(name, cond, detail = '') {
  if (!cond) failed++;
  console.log(`  ${cond ? 'ok  ' : 'FAIL'}  ${name}${detail ? '  -- ' + detail : ''}`);
}

/** Build a flat RGBA frame of one colour. */
function frame(w, h, v) {
  const data = new Uint8Array(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    data[i * 4] = v; data[i * 4 + 1] = v; data[i * 4 + 2] = v; data[i * 4 + 3] = 255;
  }
  return { width: w, height: h, data };
}

console.log('Forward+ matrix self-test\n');

// --- meanAbsDiff ---
check('identical frames measure 0', meanAbsDiff(frame(8, 8, 100), frame(8, 8, 100)) === 0);
check('a 10-level shift measures 10', meanAbsDiff(frame(8, 8, 100), frame(8, 8, 110)) === 10);
check('mismatched sizes return null', meanAbsDiff(frame(8, 8, 0), frame(4, 4, 0)) === null);

// --- judge ---
const feat = { id: 'x', minDelta: 1.0 };
check('a delta above the floor passes', judge(feat, 2.0, []).ok);
check('a delta below the floor fails', !judge(feat, 0.4, []).ok);
check('exactly zero delta fails', !judge(feat, 0, []).ok);
check('a null delta (size change) fails', !judge(feat, null, []).ok);
check('errors fail even with a large delta', !judge(feat, 99, ['GPUValidationError: boom']).ok,
  'a feature that "works" while spraying validation errors is not working');
check('the error message reaches the report',
  judge(feat, 99, ['GPUValidationError: boom']).why.includes('boom'));

// --- the feature table itself ---
const ids = FEATURES.map((f) => f.id);
check('no duplicate feature ids', new Set(ids).size === ids.length);
const unc = UNCOVERED.map((u) => u.id);
check('no duplicate uncovered ids', new Set(unc).size === unc.length);
const both = ids.filter((i) => unc.includes(i));
check('nothing is both covered and uncovered', both.length === 0, both.join(', '));
check('every uncovered entry gives a reason',
  UNCOVERED.every((u) => typeof u.why === 'string' && u.why.length > 20));
check('every feature has a known category',
  FEATURES.every((f) => Object.hasOwn(CATEGORIES, f.cat)),
  FEATURES.filter((f) => !Object.hasOwn(CATEGORIES, f.cat)).map((f) => f.id).join(', '));
// A zero or negative floor would make that feature impossible to fail, which is
// the exact failure this whole tier exists to prevent.
const slack = FEATURES.filter((f) => !(f.minDelta > 0));
check('no feature has a zero threshold', slack.length === 0, slack.map((f) => f.id).join(', '));
check('every feature has a label', FEATURES.every((f) => f.label && f.label.length > 2));

console.log(`\n  ${FEATURES.length} features covered, ${UNCOVERED.length} documented as uncovered`);
console.log(failed === 0 ? '\nRESULT: PASS' : '\nRESULT: FAIL');
process.exit(failed === 0 ? 0 : 1);
