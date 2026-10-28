extends SceneTree
# Animated grade: changes the hue shift every frame (a rebake per frame) with every control on; quits after 120 frames.
const Cases = preload("res://cases.gd")
var env: Environment
var frames := 0
func _init() -> void:
	var root3d := Node3D.new()
	env = Environment.new()
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	Cases.apply(env, "all")
	env.grading_curve_hue_vs_sat = Cases.linear_curve([Vector2(0, 0.4), Vector2(0.5, 0.6), Vector2(1, 0.4)])
	var we := WorldEnvironment.new()
	we.environment = env
	root3d.add_child(we)
	var cam := Camera3D.new()
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.position = Vector3(0, 0, 1)
	root3d.add_child(cam)
	root3d.add_child(load("res://grade_test.gd").make_chart("lattice"))
	root.add_child(root3d)
func _process(_d: float) -> bool:
	frames += 1
	env.grading_hue_shift = float(frames % 360)
	return frames >= 120
