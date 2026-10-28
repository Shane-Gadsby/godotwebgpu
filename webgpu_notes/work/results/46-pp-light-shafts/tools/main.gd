extends Node3D
# Light shafts test scene. Args after --:
#   out=<png>  shafts=0|1  scene=disk|bars|none  sx=<x> sy=<y> (sun in camera space: direction (sx, sy, -1))
#   behind=1 (sun behind the camera)  sun=<disk radiance> halo=<halo radiance>  thr=<threshold>  decay=<d>
#   intensity=<i>  frames=<n>  timing=1 (prints GPU ms averaged over frames)  msaa=0|2|4  ortho=1  light=0 (no opt-in)

var args := {}
var frame := 0
var frames := 12
var timing := false
var times: Array[float] = []

func _ready():
	var raw: PackedStringArray = OS.get_cmdline_user_args()
	if OS.has_feature("web"):
		raw = str(JavaScriptBridge.eval("decodeURIComponent(location.search.substring(1))", true)).split("&", false)
	for a in raw:
		var kv = a.split("=", true, 1)
		args[kv[0]] = kv[1] if kv.size() > 1 else "1"
	frames = int(args.get("frames", "12"))
	timing = args.get("timing", "0") == "1"
	var root3d := self

	var sx := float(args.get("sx", "0.1"))
	var sy := float(args.get("sy", "0.15"))
	var sun_dir := Vector3(sx, sy, -1.0).normalized()
	if args.get("behind", "0") == "1":
		sun_dir = Vector3(sx, sy, 1.0).normalized()

	var sky_shader := Shader.new()
	sky_shader.code = """
shader_type sky;
uniform vec3 sun_dir;
uniform float sun_radiance = 4.0;
uniform float halo_radiance = 0.0;
uniform float base = 0.2;
uniform float disk_cos = 0.9995;
void sky() {
	float c = dot(normalize(EYEDIR), sun_dir);
	float angle = acos(clamp(c, -1.0, 1.0));
	vec3 col = vec3(base);
	col += vec3(halo_radiance) * exp(-angle / 0.1);
	if (c > disk_cos) {
		col += vec3(sun_radiance);
	}
	COLOR = col;
}
"""
	var sky_mat := ShaderMaterial.new()
	sky_mat.shader = sky_shader
	sky_mat.set_shader_parameter("sun_dir", sun_dir)
	sky_mat.set_shader_parameter("sun_radiance", float(args.get("sun", "4.0")))
	sky_mat.set_shader_parameter("halo_radiance", float(args.get("halo", "0.0")))
	sky_mat.set_shader_parameter("disk_cos", cos(float(args.get("disk_deg", "1.5")) * PI / 180.0))
	var sky := Sky.new()
	sky.sky_material = sky_mat
	sky.process_mode = Sky.PROCESS_MODE_REALTIME
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0, 0, 0)
	env.reflected_light_source = Environment.REFLECTION_SOURCE_DISABLED
	# Through set() so the same script runs on the base editor, which has none of these properties.
	if args.get("shafts", "0") == "1":
		env.set("light_shafts_enabled", true)
		env.set("light_shafts_threshold", float(args.get("thr", "1.0")))
		env.set("light_shafts_decay", float(args.get("decay", "2.0")))
		env.set("light_shafts_intensity", float(args.get("intensity", "1.0")))
		if args.has("tint"):
			env.set("light_shafts_tint", Color(args["tint"]))
	if args.has("quality"):
		RenderingServer.call("environment_set_light_shafts_quality", int(args["quality"]))
	if args.get("glow", "0") == "1":
		env.glow_enabled = true
		env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var we := WorldEnvironment.new()
	we.environment = env
	root3d.add_child(we)

	var light := DirectionalLight3D.new()
	light.light_energy = float(args.get("light_energy", "0.0")) # The shafts come from the sky alone.
	light.shadow_enabled = true
	if args.get("light", "1") == "1" and args.get("shafts", "0") == "1":
		light.set("light_shafts_enabled", true)
	root3d.add_child(light)
	light.look_at_from_position(Vector3.ZERO, -sun_dir, Vector3.UP if abs(sun_dir.y) < 0.99 else Vector3.RIGHT)

	var cam := Camera3D.new()
	cam.fov = 60.0
	cam.far = 500.0
	if args.get("ortho", "0") == "1":
		cam.projection = Camera3D.PROJECTION_ORTHOGONAL
		cam.size = 40.0
	root3d.add_child(cam)
	cam.current = true

	var scene: String = args.get("scene", "bars")
	if scene == "bars":
		var mat := StandardMaterial3D.new()
		if args.get("lit", "0") == "1":
			mat.albedo_color = Color(0.6, 0.5, 0.4)
		else:
			mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			mat.albedo_color = Color(0, 0, 0)
		# A fence of vertical bars and one horizontal beam across the sun, 100 m away.
		for i in range(-6, 7):
			var bar := MeshInstance3D.new()
			var bm := BoxMesh.new()
			bm.size = Vector3(1.2, 80, 1)
			bar.mesh = bm
			bar.material_override = mat
			bar.position = Vector3(sx * 100.0 + i * 4.0, 0, -100)
			root3d.add_child(bar)
		var beam := MeshInstance3D.new()
		var beam_mesh := BoxMesh.new()
		beam_mesh.size = Vector3(200, 2.0, 1)
		beam.mesh = beam_mesh
		beam.material_override = mat
		beam.position = Vector3(0, sy * 100.0 - 4.0, -99)
		root3d.add_child(beam)

	var msaa := int(args.get("msaa", "0"))
	get_viewport().msaa_3d = [Viewport.MSAA_DISABLED, Viewport.MSAA_DISABLED, Viewport.MSAA_2X, Viewport.MSAA_2X, Viewport.MSAA_4X][msaa]
	if timing:
		RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)

func _process(_delta):
	frame += 1
	if timing and frame > 20:
		times.append(RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid()))
	if frame == frames:
		if timing:
			times.sort()
			print("GPU_MS median=%.4f n=%d" % [times[times.size() / 2], times.size()])
		if OS.has_feature("web"):
			JavaScriptBridge.eval("document.title = 'ready'")
			print("READY ", RenderingServer.get_current_rendering_method())
			set_process(false)
			return
		var img := get_viewport().get_texture().get_image()
		img.save_png(args.get("out", "out.png"))
		print("SAVED ", args.get("out", "out.png"), " ", img.get_size(), " ", RenderingServer.get_current_rendering_method())
		get_tree().quit()
