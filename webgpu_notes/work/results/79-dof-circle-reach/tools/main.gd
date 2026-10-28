extends Node3D
# Worker-63/70 web test (legacy=1: dof_legacy_gather): dof_test.gd as a main scene. Args from the page's query string, else res://config.txt.
var args := {}
var frames := 0
var out := ""

func _arg_list() -> PackedStringArray:
	var s := ""
	if OS.has_feature("web"):
		s = str(JavaScriptBridge.eval("location.search.substring(1)"))
	if s == "" and FileAccess.file_exists("res://config.txt"):
		s = FileAccess.get_file_as_string("res://config.txt").strip_edges().replace(" ", "&")
	return s.split("&", false)

func _ready() -> void:
	for a in _arg_list():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	out = args.get("out", "/tmp/out.png")
	if args.has("maxblur"):
		ProjectSettings.set_setting("rendering/camera/depth_of_field/depth_of_field_physical_max_blur_size", float(args["maxblur"]))
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
	cam.far = 200.0
	cam.near = 0.05
	var kind: String = args.get("kind", "physical")
	if kind == "physical":
		var ca := CameraAttributesPhysical.new()
		if args.has("sensor"):
			var wh: PackedStringArray = args["sensor"].split("x")
			ca.sensor_size = Vector2(float(wh[0]), float(wh[1]))
		if args.has("squeeze"):
			ca.lens_anamorphic_squeeze = float(args["squeeze"])
		ca.frustum_focal_length = float(args.get("f", "50"))
		ca.frustum_focus_distance = float(args.get("focus", "2.5"))
		ca.exposure_aperture = float(args.get("n", "1.4"))
		ca.frustum_near = 0.05
		ca.frustum_far = 200.0
		if args.get("phys", "0") == "1":
			ca.dof_physical_blur_size = true
		if args.get("legacy", "0") == "1":
			ca.dof_legacy_gather = true
		if args.has("aperture"):  # Worker-79: a custom aperture with this many blades (drawn by the circular gather).
			ca.aperture_enabled = true
			ca.aperture_blade_count = int(args["aperture"])
		cam.attributes = ca
	else:
		var cp := CameraAttributesPractical.new()
		cp.dof_blur_far_enabled = true
		cp.dof_blur_far_distance = float(args.get("far", "2.5"))
		cp.dof_blur_far_transition = float(args.get("trans", "-1"))
		cp.dof_blur_near_enabled = true
		cp.dof_blur_near_distance = float(args.get("near", "2.5"))
		cp.dof_blur_near_transition = float(args.get("trans", "-1"))
		cp.dof_blur_amount = float(args.get("amount", "0.1"))
		cam.fov = float(args.get("fov", "75"))
		if args.has("keep_width"):
			cam.keep_aspect = Camera3D.KEEP_WIDTH
		cam.attributes = cp
	root3d.add_child(cam)

	var dist := float(args.get("dist", "5"))
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
	add_child(root3d)
	if args.has("scale3d"):
		get_viewport().scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
		get_viewport().scaling_3d_scale = float(args["scale3d"])
	cam.current = true


func _process(_d: float) -> void:
	frames += 1
	if frames == 12 and OS.has_feature("web"):
		JavaScriptBridge.eval("document.title = 'ready'")
