#!/usr/bin/env bash
# Smoke check for the SPIR-V -> WGSL converter (SPIRV-Cross; webgpu_notes/work/README.md). Run from any worktree:
#
#   webgpu_notes/work/smoke.sh <scratch_dir>
#
# 1. builds bin/wgsl_convert_cli from this tree
# 2. dumps every engine variant wgsl_precompile.py builds and converts it (0 failures required)
# 3. compares bindings + override ids with the stored reference
#    (webgpu_tests/spirv_cross_eval/interface_reference.json: Tint's interface, which SPIRV-Cross matched when
#    Tint was removed; identical required, see compare_interface.py for updating it after a deliberate change),
#    and the driver's WGSL text-scan facts with webgpu_tests/wgsl_text_scan/scan_reference.txt
# 4. compiles every result in Chrome; anything invalid that is not listed in
#    smoke_expected_invalid.txt fails, and so does a listed file that has become valid (delete its line)
# Prints SMOKE OK / SMOKE FAILED as its last line. No lease needed: nothing renders.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="${1:?usage: smoke.sh <scratch_dir>}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
run_id="smoke-$(date +%Y%m%d-%H%M%S)-$$"
work="$out/$run_id"
mkdir -p "$work"/tmp
touch "$work/.start"
failed=0
fail() {
	echo "FAIL: $*"
	failed=1
}

echo "== $run_id in $repo"
echo "== build wgsl_convert_cli"
(cd "$repo" && ./drivers/webgpu/wgsl_cli/build.sh >"$work/build.log" 2>&1) || fail "wgsl_convert_cli build (see $work/build.log)"
# build.sh does not fail on a compile error (it links what compiled), so check the product's age.
[[ "$repo/bin/wgsl_convert_cli" -nt "$work/.start" ]] || fail "bin/wgsl_convert_cli was not relinked by this run (compile error? see $work/build.log)"

echo "== dump + convert every engine variant"
TMPDIR="$work/tmp" python3 "$repo/webgpu_tests/spirv_cross_eval/dump_corpus.py" "$repo" "$work" >"$work/dump.log" 2>&1 || fail "dump_corpus.py (see $work/dump.log)"
grep -E 'Results:' "$work/dump.log" || fail "no precompile result line in $work/dump.log"
grep -qE 'Results: [0-9]+ compiled, 0 glsl failures, 0 conversion failures' "$work/dump.log" || fail "precompile reported failures (see $work/dump.log)"
modules=$(find "$work/raw" -name '*.spv' | wc -l)
[[ "$modules" -gt 200 ]] || fail "only $modules modules dumped"

echo "== interface: stored reference vs this tree"
python3 "$repo/webgpu_tests/spirv_cross_eval/compare_interface.py" "$repo/webgpu_tests/spirv_cross_eval/interface_reference.json" "$work/wgsl" | tee "$work/interface.txt" | head -12
[[ "${PIPESTATUS[0]}" -eq 0 ]] || fail "interface differs from the stored reference (see $work/interface.txt)"
echo "== WGSL text-scan facts: stored reference vs this tree"
bash "$repo/webgpu_tests/wgsl_text_scan/run_tests.sh" "$work" >"$work/scan.txt" 2>&1 || fail "WGSL text-scan facts differ from the stored reference (see $work/scan.txt)"
grep -E 'against the stored reference' "$work/scan.txt" || fail "no text-scan result (see $work/scan.txt)"

echo "== Chrome validation"
OUT="$work/validation.json" node "$repo/webgpu_tests/spirv_cross_eval/validate_wgsl.mjs" "$work/wgsl" >"$work/validate.log" 2>&1
tail -n +1 "$work/validate.log" | grep -E 'adapter|files' || fail "validator produced no result (see $work/validate.log)"
python3 -I - "$work/validation.json" "$here/smoke_expected_invalid.txt" <<'PY' || failed=1
import json, sys
results = json.load(open(sys.argv[1]))
expected = set()
for line in open(sys.argv[2]):
    line = line.split("#")[0].strip()
    if line:
        expected.add(line)
(only_dir,) = results.values()
invalid = {f for f, r in only_dir.items() if not r["ok"]}
bad = sorted(invalid - expected)
fixed = sorted(expected - invalid)
for f in bad:
    r = only_dir[f]
    print(f"FAIL: invalid WGSL: {f}: {(r['errs'] or [r['scopeErr']])[0].splitlines()[0]}")
for f in fixed:
    print(f"FAIL: {f} is valid now: remove it from smoke_expected_invalid.txt")
print(f"validation: {len(only_dir)} modules, {len(invalid)} invalid, {len(invalid & expected)} of them expected")
sys.exit(1 if bad or fixed else 0)
PY

if [[ "$failed" -eq 0 ]]; then
	echo "SMOKE OK ($run_id)"
else
	echo "SMOKE FAILED ($run_id; logs in $work)"
	exit 1
fi
