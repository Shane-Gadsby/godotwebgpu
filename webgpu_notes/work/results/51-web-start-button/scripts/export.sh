#!/usr/bin/env bash
# export.sh <editor> <template_zip> <out_dir> [extra preset option lines...]
# Exports $W/proj (copied per run) for Web with the given extra [preset.0.options] lines.
set -euo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w51
EDITOR="$1" TEMPLATE="$2" OUT="$(realpath -m "$3")"; shift 3
P="$OUT.proj"
rm -rf "$P" "$OUT"; mkdir -p "$OUT"; cp -r "${PROJ:-$W/proj}" "$P"
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
"$EDITOR" --headless --path "$P" --export-release "Web" "$OUT/index.html" > "$OUT.export.log" 2>&1
ls "$OUT"
