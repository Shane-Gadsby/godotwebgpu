@tool
extends SceneTree
## Bakes the LOD subject mesh used by the Forward+ matrix.
##
## Run once, with an EDITOR build, after changing the LOD subject:
##   godot --headless --path . -s make_lod_mesh.gd
##
## The mesh is committed rather than generated at runtime because
## ImporterMesh.generate_lods() produces no LODs in a web template (it works in
## a native template, so this is web-specific), and runtime generation is not
## how a real project gets LODs anyway -- they are baked by the importer. A
## committed resource therefore tests the path users actually ship.

func _init() -> void:
	var sphere := SphereMesh.new()
	sphere.radius = 0.7
	sphere.height = 1.4
	sphere.radial_segments = 64
	sphere.rings = 32
	var im := ImporterMesh.new()
	im.add_surface(Mesh.PRIMITIVE_TRIANGLES, sphere.get_mesh_arrays())
	im.generate_lods(25.0, 60.0, [])
	var lods := im.get_surface_lod_count(0)
	var mesh := im.get_mesh()
	var err := ResourceSaver.save(mesh, "res://lod_sphere.res")
	print("[make_lod_mesh] lods=%d save=%d" % [lods, err])
	quit(0 if err == OK and lods > 0 else 1)
