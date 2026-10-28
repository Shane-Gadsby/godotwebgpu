#!/usr/bin/env bash
# export_web.sh: exports the test project with work/47's template_debug (unbaked, headless) to $W/web.
set -euo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w47
REPO=/mnt/109313D2109313D2/godot-editors/godotwebgpu-47
TEMPLATE=$(ls $REPO/bin/godot.web.template_debug.wasm32.nothreads*.zip | head -1)
OUT=$W/web
mkdir -p $OUT
cat > $W/proj/export_presets.cfg <<PRESET
[preset.0]

name="Web"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="$OUT/index.html"
script_export_mode=1

[preset.0.options]

custom_template/debug="$TEMPLATE"
custom_template/release=""
variant/extensions_support=false
variant/thread_support=false
html/export_icon=false
html/canvas_resize_policy=2
PRESET
$REPO/bin/godot.linuxbsd.editor.x86_64 --headless --path $W/proj --import >/dev/null 2>&1 || true
$REPO/bin/godot.linuxbsd.editor.x86_64 --headless --path $W/proj --export-debug "Web" $OUT/index.html 2>&1 | tail -3
rm -f $W/proj/export_presets.cfg
echo "template: $TEMPLATE"; ls -la $OUT
