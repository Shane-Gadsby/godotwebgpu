#!/usr/bin/env bash
# Smoke check for the Tint -> SPIRV-Cross swap (webgpu_notes/work/README.md). Run from any worktree:
#
#   webgpu_notes/work/smoke.sh <scratch_dir>
#
# 1. builds tint_convert_cli from this tree with both backends: bin/tint_convert_cli (SPIRV-Cross) and
#    bin/tint_convert_cli.tint (Tint, the interface reference; dropped from smoke once Tint leaves the tree)
# 2. dumps every engine variant wgsl_precompile.py builds and converts it with both (0 failures required)
# 3. compares bindings + override ids between the two (identical required)
# 4. compiles every SPIRV-Cross result in Chrome; anything invalid that is not listed in
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
mkdir -p "$work"/{tmp,ref}
touch "$work/.start"
failed=0
fail() {
	echo "FAIL: $*"
	failed=1
}

echo "== $run_id in $repo"
echo "== build tint_convert_cli (spirv-cross, tint)"
(cd "$repo" && ./drivers/webgpu/tint_cli/build.sh >"$work/build-sx.log" 2>&1) || fail "spirv-cross build (see $work/build-sx.log)"
if [[ -d "$repo/thirdparty/tint" ]]; then
	(cd "$repo" && WGSL_BACKEND=tint OUTPUT=bin/tint_convert_cli.tint ./drivers/webgpu/tint_cli/build.sh >"$work/build-tint.log" 2>&1) || fail "tint build (see $work/build-tint.log)"
fi
# build.sh does not fail on a compile error (it links what compiled), so check the products' age.
for bin in "$repo/bin/tint_convert_cli" "$repo/bin/tint_convert_cli.tint"; do
	[[ -e "$bin" ]] || continue
	[[ "$bin" -nt "$work/.start" ]] || fail "$bin was not relinked by this run (compile error? see $work/build-*.log)"
done

echo "== dump + convert every engine variant"
TMPDIR="$work/tmp" python3 "$repo/webgpu_tests/spirv_cross_eval/dump_corpus.py" "$repo" "$work" >"$work/dump.log" 2>&1 || fail "dump_corpus.py (see $work/dump.log)"
grep -E 'Results:' "$work/dump.log" || fail "no precompile result line in $work/dump.log"
grep -qE 'Results: [0-9]+ compiled, 0 glsl failures, 0 tint failures' "$work/dump.log" || fail "precompile reported failures (see $work/dump.log)"
modules=$(find "$work/raw" -name '*.spv' | wc -l)
[[ "$modules" -gt 200 ]] || fail "only $modules modules dumped"

if [[ -x "$repo/bin/tint_convert_cli.tint" && -d "$repo/thirdparty/tint" ]]; then
	ref_bad=0
	for spv in "$work"/raw/*.spv; do
		name="$(basename "${spv%.spv}")"
		"$repo/bin/tint_convert_cli.tint" "$spv" >"$work/ref/$name.wgsl" 2>/dev/null || ref_bad=$((ref_bad + 1))
	done
	echo "tint reference: $modules modules, $ref_bad failed"
	echo "== interface: tint vs spirv-cross"
	python3 "$repo/webgpu_tests/spirv_cross_eval/compare_interface.py" "$work/ref" "$work/wgsl" | tee "$work/interface.txt" | head -12
	[[ "${PIPESTATUS[0]}" -eq 0 ]] || fail "interface differs from Tint's (see $work/interface.txt)"
else
	echo "SKIP: interface comparison (no Tint in this tree)"
fi

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
