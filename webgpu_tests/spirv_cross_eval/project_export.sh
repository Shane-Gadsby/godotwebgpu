#!/usr/bin/env bash
# Export a project's web build with the editor's export-time shader baker, capturing every stage's SPIR-V
# (webgpu_notes/work/tasks/05-project-corpus.md). The project is COPIED first (exports write into .godot/).
#
#   project_export.sh <name> <project_dir> <scratch_dir> [editor_bin] [tint_convert_cli] [template_zip]
#
# The editor runs the tint_convert_cli found beside its own executable, so this copies the editor into
# <scratch>/ed with a wrapper named tint_convert_cli that copies each .spv it is handed into
# <scratch>/<name>/spv and then runs the real converter. The bake cache is off (WEBGPU_BAKE_CACHE=0), so
# every stage reaches the converter. Output: <scratch>/<name>/spv/*.spv and export.log.
# Runs in a memory-capped scope; checks free memory first.
set -euo pipefail
name="${1:?name}"; src="${2:?project}"; scratch="$(mkdir -p "${3:?scratch}" && cd "$3" && pwd)"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
editor="${4:-/mnt/109313D2109313D2/godot-editors/godotwebgpu/bin/godot.linuxbsd.editor.x86_64}"
cli="${5:-$repo/bin/tint_convert_cli}"
template="${6:-/mnt/109313D2109313D2/godot-editors/godotwebgpu/bin/godot.web.template_release.wasm32.nothreads.zip}"
ed="$scratch/ed"; proj="$scratch/$name/project"; out="$scratch/$name/export"; spv="$scratch/$name/spv"
mkdir -p "$ed" "$out" "$spv" "$scratch/tmp"
avail=$(free -g | awk '/^Mem:/{print $7}')
((avail >= 12)) || { echo "only ${avail} GB available; wait"; exit 3; }
[[ -x "$ed/godot.linuxbsd.editor.x86_64" ]] || cp "$editor" "$ed/"
cp "$cli" "$ed/tint_convert_cli.real"
cat >"$ed/tint_convert_cli" <<'WRAP'
#!/usr/bin/env bash
for a in "$@"; do case "$a" in *.spv) [ -f "$a" ] && cp -n "$a" "$SPV_CAPTURE/$(basename "$a")";; esac; done
exec "$(dirname "$0")/tint_convert_cli.real" "$@"
WRAP
chmod +x "$ed/tint_convert_cli"
if [[ ! -d "$proj" ]]; then
	mkdir -p "$proj"
	rsync -a --exclude builds --exclude '*.zip' --exclude .git "$src"/ "$proj"/
fi
cat >"$proj/export_presets.cfg" <<PRESET
[preset.0]

name="WebGPU"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="$out/index.html"
script_export_mode=1

[preset.0.options]

custom_template/debug=""
custom_template/release="$template"
variant/extensions_support=false
variant/thread_support=false
html/export_icon=false
html/canvas_resize_policy=2
PRESET
run() { systemd-run --user --scope -q -p MemoryMax=8G -p MemorySwapMax=0 "$@"; }
export SPV_CAPTURE="$spv" WEBGPU_BAKE_CACHE=0 TMPDIR="$scratch/tmp"
run "$ed/godot.linuxbsd.editor.x86_64" --headless --path "$proj" --import >"$scratch/$name/import.log" 2>&1 || true
run xvfb-run -a "$ed/godot.linuxbsd.editor.x86_64" --rendering-driver vulkan --path "$proj" --export-release WebGPU "$out/index.html" >"$scratch/$name/export.log" 2>&1 || echo "export exited $?"
echo "$name: $(ls "$spv" | wc -l) stages captured; unbaked warnings: $(grep -c 'leaving one shader stage unbaked' "$scratch/$name/export.log" || true)"
