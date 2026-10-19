extends SceneTree

# Bakes benchmark.gd's runtime-built scene into main.tscn so the export-time shader baker can see
# its materials and environment (a scene built in _ready() leaves the baker nothing to bake).
# Run: godot --headless --path . -s generate_scene.gd  (main_runtime.tscn is the script-built original)
func _init() -> void:
	var root := Node3D.new()
	root.name = "Main"
	root.set_script(load("res://benchmark.gd"))
	root._ready()
	_own(root, root)
	root.set_script(null)
	var ps := PackedScene.new()
	print("pack: ", ps.pack(root))
	print("save: ", ResourceSaver.save(ps, "res://main.tscn"))
	quit()

func _own(n: Node, owner_node: Node) -> void:
	for c in n.get_children():
		c.owner = owner_node
		_own(c, owner_node)
