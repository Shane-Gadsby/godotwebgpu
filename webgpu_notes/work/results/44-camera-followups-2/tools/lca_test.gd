extends SceneTree
# Longitudinal CA / physical DOF test: a white/black vertical edge plane at a distance, defocused.
# Args (after --): out=<png> kind=physical|practical f=<mm> n=<f-number> focus=<m> dist=<m> lca=<strength|0>
#   shape=circle|hexagon|box  (practical: far=<m> amount=<x>)
var args := {}
var frames := 0
var out := ""

func _init() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	out = args.get("out", "/tmp/out.png")
	var shape: String = args.get("shape", "circle")
	RenderingServer.camera_attributes_set_dof_blur_bokeh_shape({"box": 0, "hexagon": 1, "circle": 2}[shape])
	RenderingServer.camera_attributes_set_dof_blur_quality(int(args.get("quality", "2")), false)

	var root3d := Node3D.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	var we := WorldEnvironment.new()
	we.environment = env
	root3d.add_child(we)

	var cam := Camera3D.new()
	cam.position = Vector3(0, 0, 0)
	cam.far = 200.0
	cam.near = 0.05
	var kind: String = args.get("kind", "physical")
	var lca := float(args.get("lca", "0"))
	if kind == "physical":
		var ca := CameraAttributesPhysical.new()
		ca.frustum_focal_length = float(args.get("f", "50"))
		ca.frustum_focus_distance = float(args.get("focus", "2"))
		ca.exposure_aperture = float(args.get("n", "1.4"))
		ca.frustum_near = 0.05
		ca.frustum_far = 200.0
		if lca > 0.0:
			ca.longitudinal_chromatic_aberration_enabled = true
			ca.longitudinal_chromatic_aberration_strength = lca
		cam.attributes = ca
	else:
		var cp := CameraAttributesPractical.new()
		cp.dof_blur_far_enabled = true
		cp.dof_blur_far_distance = float(args.get("far", "3"))
		cp.dof_blur_far_transition = float(args.get("transition", "2"))
		cp.dof_blur_near_enabled = true
		cp.dof_blur_near_distance = float(args.get("near", "1.5"))
		cp.dof_blur_near_transition = float(args.get("near_transition", "1"))
		cp.dof_blur_amount = float(args.get("amount", "0.3"))
		if lca > 0.0:
			cp.longitudinal_chromatic_aberration_enabled = true
			cp.longitudinal_chromatic_aberration_strength = lca
		cam.attributes = cp
	root3d.add_child(cam)

	# Edge plane: white on the left half, black on the right, facing the camera, filling the frame.
	var dist := float(args.get("dist", "6"))
	var white := StandardMaterial3D.new()
	white.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	var level := float(args.get("level", "0.5"))
	white.albedo_color = Color(level, level, level)
	var black := StandardMaterial3D.new()
	black.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	black.albedo_color = Color(0.02, 0.02, 0.02)
	var w := dist * 4.0
	for side in [0, 1]:
		var pm := PlaneMesh.new()
		pm.size = Vector2(w, w)
		pm.orientation = PlaneMesh.FACE_Z
		var mi := MeshInstance3D.new()
		mi.mesh = pm
		mi.material_override = white if side == 0 else black
		mi.position = Vector3(-w * 0.5 if side == 0 else w * 0.5, 0, -dist)
		root3d.add_child(mi)
	root.add_child(root3d)
	cam.current = true

func _process(_d: float) -> bool:
	frames += 1
	if frames == 12:
		var img := root.get_viewport().get_texture().get_image()
		img.save_png(out)
		quit()
	return false
