#!/usr/bin/env bash
# The driver's WGSL text scans (drivers/webgpu/wgsl_text_scan.*) over the converter's output for every
# engine shader variant: what they derive must match scan_reference.txt, the facts Tint's output gave when
# SPIRV-Cross replaced it (webgpu_notes/TASKS.md §2.35; scan_reference.py says how to update it).
#
#   ./run_tests.sh <run_dir>     <run_dir> = a smoke.sh run (wgsl/ = the converter's output)
#   WRITE_REFERENCE=1 ./run_tests.sh <run_dir>   rewrite scan_reference.txt from <run_dir>/wgsl
#
# A run directory that also has ref/ (another converter build's WGSL of the same SPIR-V, e.g. a SPIRV-Cross
# patch reverted) is compared against it as well, file by file.
#
# Also runs the hand-written cases in cases/ (each file is a WGSL snippet with an expected-output file).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
run="${1:?usage: run_tests.sh <smoke run dir>}"
bin="$(mktemp -d)/scan_dump"
c++ -std=c++17 -O1 -Wall -Wextra -I"$repo/drivers/webgpu" "$here/scan_dump.cpp" "$repo/drivers/webgpu/wgsl_text_scan.cpp" -o "$bin"

fail=0
facts="$(mktemp)"
total=0
for mode in "" "--keep-rw"; do
	for sx in "$run"/wgsl/*.wgsl; do
		name="$(basename "$sx")"
		total=$((total + 1))
		# "(scanned)" records a module whose scan finds nothing, so it is still compared.
		{ echo "(scanned)"; "$bin" "$sx" $mode; } | sed "s|^|$name\t$mode\t|" >>"$facts"
	done
done
echo "corpus: $total scans of $run/wgsl"
[[ "$total" -gt 400 ]] || { echo "FAIL: only $total scans"; fail=1; }
if [[ "${WRITE_REFERENCE:-0}" == 1 ]]; then
	python3 -I "$here/scan_reference.py" --write "$here/scan_reference.txt" <"$facts" || fail=1
else
	python3 -I "$here/scan_reference.py" "$here/scan_reference.txt" <"$facts" || fail=1
fi
rm -f "$facts"

if [[ -d "$run/ref" ]]; then
	for mode in "" "--keep-rw"; do
		total=0 differ=0
		for sx in "$run"/wgsl/*.wgsl; do
			name="$(basename "$sx")"
			ref="$run/ref/$name"
			[[ -e "$ref" ]] || continue
			total=$((total + 1))
			if ! diff -q <("$bin" "$ref" $mode) <("$bin" "$sx" $mode) >/dev/null; then
				differ=$((differ + 1))
				if [[ $differ -le 5 ]]; then
					echo "DIFF ($mode) $name"
					diff <("$bin" "$ref" $mode) <("$bin" "$sx" $mode) | head -8
				fi
			fi
		done
		echo "against ref/${mode:+ $mode}: $total variants, $differ differ"
		[[ "$differ" -eq 0 ]] || fail=1
	done
fi

# VALIDATE=1: also compile the rewritten SPIRV-Cross text (read_write split, read-only to sampled, depth
# reclassification, as a device without read_write storage textures gets it) in Chrome. Needs the real GPU.
if [[ "${VALIDATE:-0}" == 1 ]]; then
	rw="$(mktemp -d)"
	for sx in "$run"/wgsl/*.wgsl; do "$bin" "$sx" --emit "$rw/$(basename "$sx")" >/dev/null; done
	OUT="$rw/validation.json" node "$repo/webgpu_tests/spirv_cross_eval/validate_wgsl.mjs" "$rw" >"$rw/validate.log" 2>&1
	python3 - "$rw/validation.json" "$repo/webgpu_notes/work/smoke_expected_invalid.txt" <<'PY' || fail=1
import json, sys
res = json.load(open(sys.argv[1]))
expected = set(l.split()[0] for l in open(sys.argv[2]) if l.strip() and not l.startswith('#')) if len(sys.argv) > 2 else set()
bad = [(d, f, v) for d, files in res.items() for f, v in files.items() if not v['ok'] and f not in expected]
print('rewritten text in Chrome: %d files, %d invalid beyond the known list' % (sum(len(x) for x in res.values()), len(bad)))
for d, f, v in bad[:5]:
	print('  ', f, v.get('errs'))
sys.exit(1 if bad else 0)
PY
fi

shopt -s nullglob
cases=0
for c in "$here"/cases/*.wgsl; do
	cases=$((cases + 1))
	exp="${c%.wgsl}.expected"
	if ! diff <("$bin" "$c") "$exp" >/dev/null; then
		echo "FAIL case $(basename "$c")"
		diff <("$bin" "$c") "$exp" | head -10
		fail=1
	fi
done
echo "cases: $cases"
[[ "$cases" -gt 0 ]] || fail=1
[[ "$fail" -eq 0 ]] && echo "WGSL TEXT SCAN OK" || { echo "WGSL TEXT SCAN FAILED"; exit 1; }
