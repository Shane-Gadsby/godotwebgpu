#!/usr/bin/env bash
# export.sh <editor> <template_zip> <out_dir> [extra preset option lines...]
# Exports $W/proj (copied per run) for Web with the start button on and the given extra [preset.0.options] lines.
# VIS=1 also enables the visibility addon, which prints the html/start_button/* options the export dialog would show.
set -euo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w69
EDITOR="$1" TEMPLATE="$2" OUT="$(realpath -m "$3")"; shift 3
P="$OUT.proj"
rm -rf "$P" "$OUT"; mkdir -p "$OUT"; cp -r "$W/proj" "$P"
if [[ "${VIS:-0}" == 1 ]]; then
  mkdir -p "$P/addons"; cp -r "$W/vis_addon" "$P/addons/vis"
  printf '\n[editor_plugins]\n\nenabled=PackedStringArray("res://addons/vis/plugin.cfg")\n' >> "$P/project.godot"
fi
{
cat <<PRESET
[preset.0]

name="Web"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="$OUT/index.html"

[preset.0.options]

custom_template/debug=""
custom_template/release="$TEMPLATE"
variant/extensions_support=false
variant/thread_support=false
html/export_icon=false
PRESET
for l in "$@"; do echo "$l"; done
} > "$P/export_presets.cfg"
"$EDITOR" --headless --path "$P" --import >/dev/null 2>&1 || true
if [[ "${VIS:-0}" == 1 ]]; then
  "$EDITOR" --headless --path "$P" --export-release "Web" "$OUT/index.html" > "$OUT.export.log" 2>&1 || true
else
  xvfb-run -a "$EDITOR" --rendering-driver vulkan --path "$P" --export-release "Web" "$OUT/index.html" > "$OUT.export.log" 2>&1
fi
ls "$OUT" | tr '\n' ' '; echo
