#!/usr/bin/env bash
# Export the Forward+ fixture to ./export using the freshly built template.
# Kept separate from run_forward_plus.mjs so a matrix run never silently tests a
# stale binary: this prints the checksum pair that proves which engine it used.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
EDITOR="${GODOT_EDITOR_BIN:-$REPO/bin/godot.linuxbsd.editor.x86_64}"
TEMPLATE="${GODOT_TEMPLATE_ZIP:-$REPO/bin/godot.web.template_release.wasm32.nothreads.zip}"
PROJECT="$HERE/godot/fp_check"

[[ -x "$EDITOR" ]]   || { echo "no editor at $EDITOR (build it first)"; exit 1; }
[[ -f "$TEMPLATE" ]] || { echo "no template at $TEMPLATE (build it first)"; exit 1; }

cat > "$PROJECT/export_presets.cfg" <<PRESET
[preset.0]

name="WebGPU"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="$HERE/export/index.html"
script_export_mode=1

[preset.0.options]

custom_template/debug=""
custom_template/release="$TEMPLATE"
variant/extensions_support=false
variant/thread_support=false
html/export_icon=false
html/canvas_resize_policy=2
PRESET

mkdir -p "$HERE/export"
"$EDITOR" --headless --path "$PROJECT" --import >/dev/null 2>&1 || true
"$EDITOR" --headless --path "$PROJECT" --export-release "WebGPU" "$HERE/export/index.html" >/dev/null
rm -f "$PROJECT/export_presets.cfg"

echo "exported to $HERE/export"
md5sum "$REPO/bin/godot.web.template_release.wasm32.nothreads.wasm" "$HERE/export/index.wasm" 2>/dev/null || true
