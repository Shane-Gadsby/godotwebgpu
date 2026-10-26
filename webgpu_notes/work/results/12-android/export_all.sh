#!/usr/bin/env bash
# export_all.sh <tag>: export the phone scene set with $W/<tag>/ editor (+ its converter) and template.
# tag=sc uses the lead's SPIRV-Cross template; tag=tint the Tint-everywhere one at the same commit.
set -euo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/06ad18c7-2f4b-4e67-91c1-7151308515f2/scratchpad/w12
T=/mnt/109313D2109313D2/godot-editors/godotwebgpu-12/webgpu_tests
tag=$1 out=$W/exp/$tag
export GODOT_EDITOR_BIN=$W/$tag/godot.linuxbsd.editor.x86_64 GODOT_TEMPLATE_ZIP=$W/$tag/template.zip WEBGPU_BAKE_CACHE=0
[[ -f $GODOT_TEMPLATE_ZIP ]] || { echo "no template for $tag"; exit 1; }
mkdir -p "$out"
cd $T/forward_plus
BAKE=1 ./export.sh && rm -rf "$out/fp" && cp -r export_baked "$out/fp"
BAKE=1 ./export_heavy.sh && rm -rf "$out/heavy" && cp -r export_heavy_baked "$out/heavy"
cd $T/scene_smoketest
SMOKETEST_BAKE=1 SMOKETEST_EXPORTS_DIR=$out node run_scenes.mjs --export-only --scenes benchmark_postfx,demo_3d_platformer,stress_3d_platformer
ls -la "$out"/*/ | grep -E "^/|index\.(wasm|pck)|shaders"
