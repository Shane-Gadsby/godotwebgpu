@tool
extends SceneTree
## Bakes the VoxelGI data used by the Forward+ matrix.
##
## Run with an EDITOR build after changing the scene's static geometry:
##   godot --path . -s make_bake_assets.gd --rendering-driver vulkan
##
## Committed rather than baked at runtime for the same reason as lod_sphere.res:
## a shipped project bakes VoxelGI in the editor and loads the result, so that
## is the path worth testing. Baking inside the matrix also made the measurement
## non-reproducible, since the bake competed with the settle window.

func _init() -> void:
	var scene: PackedScene = load("res://main.tscn")
	var node := scene.instantiate()
	root.add_child(node)
	for i in 8:
		await process_frame
	var vg: VoxelGI = node.voxel_gi
	vg.visible = true
	vg.bake(node, false)
	var err := ResourceSaver.save(vg.data, "res://voxel_gi_data.res")
	print("[make_bake_assets] data=%s save=%d" % [str(vg.data != null), err])
	quit(0 if err == OK and vg.data != null else 1)
