#!/usr/bin/env bash
# Bake the LightmapGI data of godot/fp_check/lightmap_room.tscn (the matrix's lightmap_gi arm).
#
# Run after changing lightmap_room.tscn; the result is committed (lightmap_room.lmbake, lightmap_room.exr and its
# .import), the same way voxel_gi_data.res is.
#
# LightmapGI cannot be baked from a plain script: LightmapGI::bake() is not exposed to scripts (its binding is commented
# out in lightmap_gi.cpp), the lightmapper needs a RenderingDevice (so no --headless), and the result has to be imported
# by the editor. So this copies the fixture to a scratch project, adds a throwaway editor plugin that opens the scene,
# selects the LightmapGI node and presses the 3D toolbar's own "Bake Lightmaps" button -- the click a user makes --
# runs the editor under Xvfb with Vulkan, and copies the bake back. No GUI interaction is needed.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
EDITOR="${GODOT_EDITOR_BIN:-$REPO/bin/godot.linuxbsd.editor.x86_64}"
PROJECT="$HERE/godot/fp_check"
command -v xvfb-run >/dev/null || { echo "xvfb-run is required"; exit 2; }
[[ -x "$EDITOR" ]] || { echo "no editor at $EDITOR"; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp "$PROJECT"/project.godot "$PROJECT"/lightmap_room.tscn "$WORK"/
mkdir -p "$WORK/addons/fp_bake"
cat >"$WORK/addons/fp_bake/plugin.cfg" <<'CFG'
[plugin]
name="fp_bake"
description="Bakes lightmap_room.tscn and quits."
author="forward_plus"
version="1"
script="plugin.gd"
CFG
cat >"$WORK/addons/fp_bake/plugin.gd" <<'GD'
@tool
extends EditorPlugin

func _enter_tree() -> void:
	_run.call_deferred()

func _find(n: Node, text: String) -> Button:
	if n is Button and (n as Button).text == text:
		return n
	for c in n.get_children(true):
		var b := _find(c, text)
		if b:
			return b
	return null

func _run() -> void:
	await get_tree().create_timer(2.0).timeout
	EditorInterface.open_scene_from_path("res://lightmap_room.tscn")
	await get_tree().create_timer(1.0).timeout
	var lm: LightmapGI = EditorInterface.get_edited_scene_root().get_node("LightmapGI")
	# The bake writes next to its LightmapGIData; without one it opens a file dialog.
	ResourceSaver.save(LightmapGIData.new(), "res://lightmap_room.lmbake")
	lm.light_data = load("res://lightmap_room.lmbake")
	EditorInterface.edit_node(lm)
	await get_tree().process_frame
	var button := _find(EditorInterface.get_base_control(), "Bake Lightmaps")
	if not button:
		print("[BAKE] FAILED no Bake Lightmaps button")
		get_tree().quit(1)
		return
	button.pressed.emit() # synchronous: returns when the bake and the import are done
	EditorInterface.save_scene()
	await get_tree().create_timer(2.0).timeout
	var data := lm.light_data
	print("[BAKE] %s textures=%d" % ["DONE" if data and not data.lightmap_textures.is_empty() else "FAILED",
		data.lightmap_textures.size() if data else 0])
	get_tree().quit(0)
GD
printf '\n[editor_plugins]\n\nenabled=PackedStringArray("res://addons/fp_bake/plugin.cfg")\n' >>"$WORK/project.godot"

"$EDITOR" --headless --path "$WORK" --import >/dev/null 2>&1 || true
out="$(xvfb-run -a "$EDITOR" --rendering-driver vulkan -e --path "$WORK" 2>&1 || true)"
grep -E "\[BAKE\]|Done baking|ERROR" <<<"$out" | head -20
grep -q "\[BAKE\] DONE" <<<"$out" || { echo "bake failed"; exit 1; }

cp "$WORK"/lightmap_room.tscn "$WORK"/lightmap_room.lmbake "$PROJECT"/
for f in "$WORK"/lightmap_room*.exr "$WORK"/lightmap_room*.exr.import; do cp "$f" "$PROJECT"/; done
ls -la "$PROJECT"/lightmap_room*
