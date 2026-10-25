#!/usr/bin/env bash
# Task 18: export the scratch cameraSim copy for the web, baked with a given converter.
#   export.sh <tag> <scratch_project> <out_dir> <converter_binary>
# The lead's editor runs the tint_convert_cli found beside its own executable, so the editor is copied into
# <out_dir>/../ed-<tag>/ with the chosen converter beside it. The preset is the user's own, with only the export
# path and the custom release template (the lead's nothreads release zip, built with the editor) changed.
# The project is exported from its own per-tag copy (exports write into .godot/).
set -euo pipefail
tag="${1:?tag}"; src="${2:?project}"; out="${3:?out}"; cli="${4:?converter}"
main=/mnt/109313D2109313D2/godot-editors/godotwebgpu
editor="$main/bin/godot.linuxbsd.editor.x86_64"
template="$main/bin/godot.web.template_release.wasm32.nothreads.zip"
base="$(dirname "$out")"; ed="$base/ed-$tag"; proj="$base/project-$tag"
mkdir -p "$ed" "$out" "$base/tmp"
avail=$(free -g | awk '/^Mem:/{print $7}')
((avail >= 12)) || { echo "only ${avail} GB available; wait"; exit 3; }
cp "$editor" "$ed/"
cp "$cli" "$ed/tint_convert_cli"
md5sum "$editor" "$template" "$cli" >"$out.inputs.md5"
rsync -a --delete --exclude /export_presets.cfg "$src"/ "$proj"/
sed -e "s|^export_path=.*|export_path=\"$out/index.html\"|" \
	-e "s|^custom_template/release=.*|custom_template/release=\"$template\"|" \
	"$src/export_presets.cfg" >"$proj/export_presets.cfg"
grep -E '^(export_path|custom_template/release)=' "$proj/export_presets.cfg"
run() { systemd-run --user --scope -q -p MemoryMax=8G -p MemorySwapMax=0 "$@"; }
export WEBGPU_BAKE_CACHE=0 TMPDIR="$base/tmp"
run "$ed/godot.linuxbsd.editor.x86_64" --headless --path "$proj" --import >"$base/import-$tag.log" 2>&1 || true
t0=$(date +%s)
run xvfb-run -a "$ed/godot.linuxbsd.editor.x86_64" --rendering-driver vulkan --path "$proj" --export-release Web "$out/index.html" >"$base/export-$tag.log" 2>&1 || echo "export exited $?"
echo "$tag: export took $(( $(date +%s) - t0 )) s; unbaked warnings: $(grep -c 'leaving one shader stage unbaked' "$base/export-$tag.log" || true)"
ls -la "$out"
