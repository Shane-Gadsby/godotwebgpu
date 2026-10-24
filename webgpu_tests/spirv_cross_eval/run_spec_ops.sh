#!/usr/bin/env bash
# What SPIRV-Cross's WGSL backend does with specialization constants, module by module (webgpu_notes/TASKS.md
# §2.35, spirv_preprocess::spec_constants_overridable()). Needs bin/tint_convert_cli (SPIRV-Cross build), spirv-as and
# a headed system Chrome with a GPU (take the `gpu` lease: it builds compute pipelines).
#
#   webgpu_tests/spirv_cross_eval/run_spec_ops.sh <scratch_dir>
#
# 1. override path: each module converted with `--raw`, built with default and alternate pipeline constants, values
#    compared. 2. legacy path: spec defaults patched to the alternate values, converted through run_pipeline() (which
#    freezes what spec_constants_overridable() rejects), built with no constants. Both lists below are known
#    failures; one that starts passing fails the run so it gets removed.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="${1:?usage: run_spec_ops.sh <scratch_dir>}"
cli="$repo/bin/tint_convert_cli"
mkdir -p "$out/raw" "$out/legacy"
python3 "$here/spec_op_probes.py" "$out/ops"
for spv in "$out"/ops/*.spv; do
	n="$(basename "${spv%.spv}")"
	"$cli" --raw "$spv" >"$out/raw/$n.wgsl" 2>/dev/null || : >"$out/raw/$n.wgsl"
	python3 -I "$here/patch_spec_defaults.py" "$spv" "$out/legacy/$n.alt.spv" "$out/ops/manifest.json"
	"$cli" "$out/legacy/$n.alt.spv" >"$out/legacy/$n.alt.wgsl" 2>/dev/null || : >"$out/legacy/$n.alt.wgsl"
done
echo "== path of each module"
"$cli" --spec-info "$out"/ops/*.spv | sed "s|$out/ops/||;s|\.spv||" | awk '{printf "%s=%s  ", $1, $2} END {print ""}'
echo "== override path (--raw, pipeline constants)"
# op_SMod: GLSL leaves negative operands undefined and SPIRV-Cross emits WGSL's truncated %.
# vector_shuffle / composite_use_runtime: SPIRV-Cross loops without converging. local_array: override-sized
# function-scope arrays are not WGSL.
EXPECT_FAIL=op_SMod,vector_shuffle,composite_use_runtime,local_array_sized_by_spec \
	node "$here/spec_op_check.mjs" "$out/ops" "$out/raw"
echo "== legacy path (patched defaults, full pipeline, no constants)"
# composite_extract / vector_shuffle / composite_insert: freeze_spec_constant_ops() evaluates CompositeExtract and
# friends of a spec composite to 0, with any backend (Tint gave the same); GLSL cannot produce them.
LEGACY=1 EXPECT_FAIL=op_SMod,composite_extract,vector_shuffle,composite_insert \
	node "$here/spec_op_check.mjs" "$out/ops" "$out/legacy"
