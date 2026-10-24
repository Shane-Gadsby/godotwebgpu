#!/usr/bin/env bash
# SPIRV-Cross WGSL backend against Tint on every engine shader variant (webgpu_notes/TASKS.md §2.35).
#
#   ./run.sh <out_dir> <spirv-cross binary>
#
# 1. dump_corpus.py: raw SPIR-V + Tint WGSL of every variant wgsl_precompile.py builds
# 2. our preprocessing passes alone (tint_convert_cli's TINT_DEBUG_DUMP_PREPROCESSED), into <out>/pre
# 3. spirv-cross --wgsl on raw and on preprocessed SPIR-V; failures tallied by message
# 4. compare_interface.py: bindings and override ids, Tint vs spirv-cross on preprocessed input
# 5. validate_wgsl.mjs: every output through Chrome's WGSL compiler (real GPU, headed)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="${1:?usage: run.sh <out_dir> <spirv-cross>}"
spirv_cross="${2:?usage: run.sh <out_dir> <spirv-cross>}"
tint_cli="$repo/bin/tint_convert_cli"

mkdir -p "$out"/{raw,tint,pre,sx_raw,sx_pre,err_raw,err_pre,tmp}
TMPDIR="$out/tmp" python3 "$here/dump_corpus.py" "$repo" "$out"

for spv in "$out"/raw/*.spv; do
	TINT_DEBUG_DUMP_PREPROCESSED="$out/pre/$(basename "$spv")" "$tint_cli" "$spv" >/dev/null 2>&1 || true
done

convert() {
	local src="$1" dst="$2" err="$3" ok=0 bad=0
	for spv in "$src"/*.spv; do
		local name
		name="$(basename "${spv%.spv}")"
		if "$spirv_cross" --wgsl "$spv" --output "$dst/$name.wgsl" 2>"$err/$name.txt" >/dev/null; then
			ok=$((ok + 1))
		else
			bad=$((bad + 1))
		fi
	done
	echo "$(basename "$src"): $ok converted, $bad failed"
	grep -h -v '^Warning' "$err"/*.txt 2>/dev/null | sed 's/%[0-9]*/%N/g' | sort | uniq -c | sort -rn | head -10 || true
}
convert "$out/raw" "$out/sx_raw" "$out/err_raw"
convert "$out/pre" "$out/sx_pre" "$out/err_pre"

python3 "$here/compare_interface.py" "$out/tint" "$out/sx_pre" || true
OUT="$out/validation.json" node "$here/validate_wgsl.mjs" "$out/tint" "$out/sx_pre" "$out/sx_raw" || true
