#!/usr/bin/env bash
# MetaHuman importer checks (webgpu_notes/TASKS.md §2.33) against a real MetaHuman export.
#
#   ./run.sh <export.zip | export dir> [--shots] [--export]
#
# Builds a throwaway project from the export, imports it with the editor, and runs verify.gd:
# skeleton, one shared skin, LOD ranges, eyes on their joints, the face rig moving the jaw and the
# body rig's correctives following an elbow bend. --shots also renders shots/ (offscreen, lavapipe
# under Xvfb, so no window opens). --export exports bench.tscn for the web with the WebGPU template,
# for perf_guard's gpu_time.mjs / api_profile.mjs. No export ships in the repo: MetaHumans are not
# ours to redistribute.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
EDITOR="${GODOT_EDITOR_BIN:-$REPO/bin/godot.linuxbsd.editor.dev.x86_64}"
TEMPLATE="${GODOT_TEMPLATE_ZIP:-$REPO/bin/godot.web.template_release.wasm32.nothreads.zip}"
WORK="${MH_WORK_DIR:-$HERE/work}"

SOURCE="${1:-}"
[[ -n "$SOURCE" ]] || { echo "usage: $0 <export.zip | export dir> [--shots] [--export]"; exit 1; }
shift
SHOTS=0
EXPORT=0
for arg in "$@"; do
    case "$arg" in
        --shots) SHOTS=1 ;;
        --export) EXPORT=1 ;;
        *) echo "unknown option $arg"; exit 1 ;;
    esac
done
[[ -x "$EDITOR" ]] || { echo "no editor at $EDITOR (build it first)"; exit 1; }

PROJECT="$WORK/project"
mkdir -p "$PROJECT"
if [[ ! -f "$PROJECT/MH/head.dna" ]]; then
    rm -rf "$PROJECT/MH"
    if [[ -f "$SOURCE" ]]; then
        TMP="$WORK/unzip"
        rm -rf "$TMP"
        mkdir -p "$TMP"
        unzip -q "$SOURCE" -d "$TMP" -x '__MACOSX/*' '*.DS_Store'
        SOURCE="$(dirname "$(find "$TMP" -name head.dna | head -1)")"
    fi
    [[ -f "$SOURCE/head.dna" ]] || { echo "no head.dna in $SOURCE"; exit 1; }
    mkdir -p "$PROJECT/MH"
    cp -r "$SOURCE"/. "$PROJECT/MH/"
fi

cp "$HERE/verify.gd" "$HERE/shot.gd" "$HERE/bench.gd" "$PROJECT/"
sed -i 's#res://Ava/#res://MH/#g' "$PROJECT"/*.gd
cat > "$PROJECT/project.godot" <<'PROJECT_GODOT'
config_version=5

[application]
config/name="MetaHuman importer test"
config/features=PackedStringArray("4.8", "Forward Plus")
run/main_scene="res://bench.tscn"

[display]
window/vsync/vsync_mode=0

[rendering]
renderer/rendering_method="forward_plus"
PROJECT_GODOT
cat > "$PROJECT/bench.tscn" <<'BENCH'
[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://bench.gd" id="1"]

[node name="Bench" type="Node3D"]
script = ExtResource("1")
BENCH

echo "== import"
"$EDITOR" --headless --path "$PROJECT" --import >"$WORK/import.log" 2>&1 || true
grep -E "MetaHuman" "$WORK/import.log" || true

echo "== verify"
"$EDITOR" --headless --path "$PROJECT" -s verify.gd 2>&1 | grep -E "^(PASS|FAIL|DONE)" | tee "$WORK/verify.log"
grep -q "DONE failures=0" "$WORK/verify.log"

if [[ "$SHOTS" == 1 ]]; then
    echo "== shots"
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a -s "-screen 0 1280x1280x24" \
        "$EDITOR" --path "$PROJECT" --rendering-driver vulkan -s shot.gd -- "$WORK/shots" 2>&1 | grep -E "^saved" || true
    echo "shots in $WORK/shots"
fi

if [[ "$EXPORT" == 1 ]]; then
    [[ -f "$TEMPLATE" ]] || { echo "no template at $TEMPLATE (build it first)"; exit 1; }
    OUT="$WORK/export"
    mkdir -p "$OUT"
    cat > "$PROJECT/export_presets.cfg" <<PRESET
[preset.0]

name="WebGPU"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter="MH/Clothing/*,MH/Geometry/*,MH/*.json,MH/body.dna,verify.gd,shot.gd"
export_path="$OUT/index.html"
script_export_mode=1

[preset.0.options]

custom_template/debug=""
custom_template/release="$TEMPLATE"
variant/extensions_support=false
variant/thread_support=false
html/export_icon=false
html/canvas_resize_policy=2
PRESET
    # Baking needs a windowed editor (--headless skips the export-time shader baker).
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a "$EDITOR" --rendering-driver vulkan --path "$PROJECT" --export-release "WebGPU" "$OUT/index.html" >"$WORK/export.log" 2>&1 || true
    ls -la "$OUT"
    echo "exported to $OUT; measure with ../perf_guard/gpu_time.mjs --dir $OUT --uncapped"
fi
