@tool
extends EditorPlugin

# An interactive export (not --export-release, which never draws the 3D viewport)
# of the Web preset, from inside a windowed editor whose 3D viewport has FSR 2
# and TAA on. The editor keeps drawing frames between the export's steps, so the
# shader baker's WebGPU capability answers must not reach them. run.sh judges the
# log; this prints the facts it needs as [BAKE-ISO] lines and quits.

# EditorExportPlatform instances are not reachable from scripts except through an
# export plugin's callbacks, which the editor runs for every platform when it
# builds the presets' option lists.
class Grab extends EditorExportPlugin:
	var web: EditorExportPlatform
	func _get_name() -> String:
		return "bake_isolation_grab"
	func _supports_platform(p: EditorExportPlatform) -> bool:
		if p.get_os_name() == "Web":
			web = p
		return true
	func _get_export_options(p: EditorExportPlatform) -> Array[Dictionary]:
		if p.get_os_name() == "Web":
			web = p
		return []

var grab := Grab.new()
var frames := 0
var state := 0
var exporting := false
var frames_during_export := 0

func _enter_tree() -> void:
	add_export_plugin(grab)
	RenderingServer.frame_post_draw.connect(_on_frame_post_draw)
	EditorInterface.open_scene_from_path("res://main.tscn")
	EditorInterface.set_main_screen_editor("3D")

func _exit_tree() -> void:
	RenderingServer.frame_post_draw.disconnect(_on_frame_post_draw)
	remove_export_plugin(grab)

func _on_frame_post_draw() -> void:
	if exporting:
		frames_during_export += 1

func _quit(code: int) -> void:
	EditorInterface.get_base_control().get_tree().quit(code)

func _process(_delta: float) -> void:
	frames += 1
	# Let the editor settle and draw the 3D viewport with FSR 2 for a while first.
	if state == 0 and frames > 240:
		state = 1
		var vp := EditorInterface.get_editor_viewport_3d(0)
		print("[BAKE-ISO] viewport scaling_3d_mode=%d use_taa=%s" % [vp.scaling_3d_mode, vp.use_taa])
		if grab.web == null:
			print("[BAKE-ISO] FAIL no Web export platform reached the plugin")
			_quit(1)
			return
		var presets := grab.web.get_current_presets()
		if presets.is_empty():
			print("[BAKE-ISO] FAIL no Web preset")
			_quit(1)
			return
		print("[BAKE-ISO] exporting")
		exporting = true
		var err := grab.web.export_project(presets[0], false, OS.get_environment("BAKE_ISO_OUT"), 0, true)
		exporting = false
		print("[BAKE-ISO] export err=%d frames_during_export=%d" % [err, frames_during_export])
		state = 2
		frames = 0
	elif state == 2 and frames > 60:
		# Some frames after the bake too: the editor must keep rendering cleanly.
		print("[BAKE-ISO] done")
		_quit(0)
