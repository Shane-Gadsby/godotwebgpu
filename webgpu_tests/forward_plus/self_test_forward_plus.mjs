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

import { meanAbsDiff, judge, judgeParity, judgeInfo, parseInfo, withFixedFps } from './run_forward_plus.mjs';
import { FEATURES, UNCOVERED, CATEGORIES, PARITY_TOLERANCE } from './features.mjs';

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

// --- judgeParity ---
const pf = { id: 'p' };
check('frames matching native pass', judgeParity(pf, 0.002, 0.004).ok);
check('an "on" frame off by more than the tolerance fails', !judgeParity(pf, 0.002, PARITY_TOLERANCE * 2).ok,
  'a feature that renders, but differently from native, is what parity exists to catch');
check('an "off" frame off by more than the tolerance fails too', !judgeParity(pf, PARITY_TOLERANCE * 2, 0.002).ok);
check('a missing reference fails rather than passing', !judgeParity(pf, null, 0).ok);
check('a feature tolerance replaces the default', judgeParity({ id: 'q', parityTol: 1.0 }, 0.5, 0.5).ok);

// --- render-info arms (occlusion culling: same frame, fewer objects) ---
const occ = { id: 'o', minDelta: 0, maxDelta: 0.02, infoDrop: { objects: 15 } };
const i29 = parseInfo('objects=29 primitives=13490 draws=9'), i8 = parseInfo('objects=8 primitives=9026 draws=7');
check('render info parses', i29 && i29.objects === 29 && i29.draws === 9);
check('an unchanged frame with fewer objects passes', judge(occ, 0, [], i29, i8).ok);
check('the same object count fails', !judge(occ, 0, [], i29, i29).ok,
  'occlusion culling that culls nothing must not pass as "frame unchanged"');
check('a changed frame fails even with fewer objects', !judge(occ, 0.5, [], i29, i8).ok,
  'culling something visible is a bug, not an optimization');
check('missing render info fails', !judge(occ, 0, [], null, i8).ok);
check('a feature without infoDrop has no info verdict', judgeInfo({ id: 'n' }, i29, i8) === null);
check('the native side is named in its verdict', judgeInfo(occ, i29, i29, ' (native)').why.includes('native'));

// --- withFixedFps ---
check('an empty argument list gets the fixed timestep', withFixedFps('{"args":[],"x":1}') === '{"args":["--fixed-fps","60"],"x":1}');
check('existing arguments are kept', withFixedFps('"args":["--verbose"]') === '"args":["--fixed-fps","60","--verbose"]');

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
// Parity-only arms are exempt: the delta test never runs on them, and their
// correct result is "no change".
// So are arms whose correct frame is unchanged and that bound the change from
// above instead: render-info arms (judged on the counts they report) and the
// NaN-containment arms (a NaN pixel must not spread). Such an arm needs a real,
// small bound: a maxDelta of 0 would fail on run-to-run noise, and a large one
// would pass the spread it exists to catch.
const bounded = (f) => f.maxDelta !== undefined && f.maxDelta > 0 && f.maxDelta < 1;
const slack = FEATURES.filter((f) => !f.parityOnly && !(f.minDelta > 0) && !bounded(f));
check('no feature has a zero threshold', slack.length === 0, slack.map((f) => f.id).join(', '));
// A parity tolerance at or above a feature's own effect would let the effect
// vanish entirely and still pass.
const loose = FEATURES.filter((f) => f.parityTol !== undefined && !f.parityOnly && f.parityTol >= f.minDelta * 3);
check('no parity tolerance swallows its feature', loose.length === 0, loose.map((f) => f.id).join(', '));
check('every knownFail gives a reason',
  FEATURES.every((f) => f.knownFail === undefined || (typeof f.knownFail === 'string' && f.knownFail.length > 20)));
check('every feature has a label', FEATURES.every((f) => f.label && f.label.length > 2));

console.log(`\n  ${FEATURES.length} features covered, ${UNCOVERED.length} documented as uncovered`);
console.log(failed === 0 ? '\nRESULT: PASS' : '\nRESULT: FAIL');
process.exit(failed === 0 ? 0 : 1);
