#!/usr/bin/env bash
# Export the occlusion benchmark: ./export.sh <template.zip> <out dir> [threads=no|yes]
# GODOT_EDITOR_BIN overrides the editor (default: this tree's bin/). BAKE=1 exports under xvfb-run so the shader
# baker runs (--headless skips it, TASKS.md 2.29).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
EDITOR="${GODOT_EDITOR_BIN:-$REPO/bin/godot.linuxbsd.editor.x86_64}"
TEMPLATE="$(realpath "$1")" OUT="$(realpath -m "$2")" THREADS="${3:-no}"
PROJECT="$HERE/godot"
TS=false; [[ "$THREADS" == yes ]] && TS=true
cat > "$PROJECT/export_presets.cfg" <<PRESET
[preset.0]

name="Web"
platform="Web"
runnable=true
export_filter="all_resources"
export_path="$OUT/index.html"

[preset.0.options]

custom_template/debug=""
custom_template/release="$TEMPLATE"
variant/extensions_support=false
variant/thread_support=$TS
html/export_icon=false
html/canvas_resize_policy=0
PRESET
mkdir -p "$OUT"
"$EDITOR" --headless --path "$PROJECT" --import >/dev/null 2>&1 || true
if [[ "${BAKE:-0}" == 1 ]]; then
	xvfb-run -a "$EDITOR" --rendering-driver vulkan --path "$PROJECT" --export-release "Web" "$OUT/index.html" >/dev/null 2>&1
else
	"$EDITOR" --headless --path "$PROJECT" --export-release "Web" "$OUT/index.html" >/dev/null 2>&1
fi
rm -f "$PROJECT/export_presets.cfg"
md5sum "$TEMPLATE" "$OUT/index.wasm"
