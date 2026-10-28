extends SceneTree
# Micro shadows test: an orthographic camera looks straight down at a quad lit by one directional light. The quad's
# material has an AO gradient along x (AO = 0..1 left to right) and a normal map that tilts the normal about the
# tangent along y (-60..60 degrees, top to bottom), Lambert diffuse, no specular, no ambient, linear tonemapper.
# Rendering micro shadows on and off and dividing gives the micro-shadow factor per pixel, to compare with
# HDRP's ComputeMicroShadowing(AO, N.L, opacity).
# Args (after --): out=<path.exr> elev=<deg above horizon> azim=<deg> micro=0|1 opacity=<0..1> shadow=0|1
#   normal=0|1 (flat when 0) frames=<n>
var args := {}
var frames := 0
var out := ""
const N := 256
const TILT_MAX := 60.0


func _init() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	out = args.get("out", "/tmp/out.exr")

	var root3d := Node3D.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	env.reflected_light_source = Environment.REFLECTION_SOURCE_DISABLED
	if args.get("micro", "0") == "1":
		env.micro_shadows_enabled = true
		env.micro_shadows_opacity = float(args.get("opacity", "1"))
	var we := WorldEnvironment.new()
	we.environment = env
	root3d.add_child(we)

	var cam := Camera3D.new()
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 2.0
	cam.near = 0.05
	cam.far = 10.0
	cam.position = Vector3(0, 3, 0)
	cam.rotation_degrees = Vector3(-90, 0, 0)
	root3d.add_child(cam)

	# AO: x gradient. Normal map: tilt about the tangent (x) axis varies along y.
	var ao_img := Image.create(N, N, false, Image.FORMAT_RGB8)
	var nm_img := Image.create(N, N, false, Image.FORMAT_RGB8)
	for y in N:
		var t := deg_to_rad(lerpf(-TILT_MAX, TILT_MAX, (y + 0.5) / N))
		var n := Vector3(0.0, sin(t), cos(t)) if args.get("normal", "1") == "1" else Vector3(0, 0, 1)
		var nc := Color(n.x * 0.5 + 0.5, n.y * 0.5 + 0.5, n.z * 0.5 + 0.5)
		for x in N:
			var ao := (x + 0.5) / N
			ao_img.set_pixel(x, y, Color(ao, ao, ao))
			nm_img.set_pixel(x, y, nc)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(1, 1, 1)
	mat.roughness = 1.0
	mat.metallic = 0.0
	mat.metallic_specular = 0.0
	mat.diffuse_mode = BaseMaterial3D.DIFFUSE_LAMBERT
	mat.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	mat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	mat.ao_enabled = args.get("ao", "1") == "1"
	mat.ao_texture = ImageTexture.create_from_image(ao_img)
	mat.ao_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_RED
	mat.ao_light_affect = 0.0
	mat.normal_enabled = true
	mat.normal_texture = ImageTexture.create_from_image(nm_img)
	var quad := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(2, 2)
	quad.mesh = pm
	quad.material_override = mat
	root3d.add_child(quad)

	if args.get("occluder", "0") == "1":
		# A small box above the quad, out of view (the camera is orthographic), casting a shadow on part of it.
		var occ := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(0.6, 0.05, 0.6)
		occ.mesh = bm
		occ.position = Vector3(0.3, 0.5, 0.0)
		occ.layers = 1
		occ.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY
		root3d.add_child(occ)

	var light := DirectionalLight3D.new()
	light.light_energy = 0.8
	light.light_specular = 0.0
	light.shadow_enabled = args.get("shadow", "0") == "1"
	light.rotation_degrees = Vector3(-float(args.get("elev", "45")), float(args.get("azim", "0")), 0)
	root3d.add_child(light)

	if args.get("hdr", "1") == "1":
		root.use_hdr_2d = true
	root.add_child(root3d)


var gpu_times: Array[float] = []


func _process(_delta: float) -> bool:
	frames += 1
	if args.get("cost", "0") == "1":
		# GPU time of the viewport, frames 100..400 (cost mode: no image saved).
		if frames == 2:
			RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
		elif frames > 100:
			gpu_times.append(RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid()))
		if frames == 400:
			gpu_times.sort()
			print("saved cost median_gpu_ms %.4f p10 %.4f p90 %.4f" % [gpu_times[gpu_times.size() / 2], gpu_times[gpu_times.size() / 10], gpu_times[gpu_times.size() * 9 / 10]])
			return true
		return false
	if frames == int(args.get("frames", "12")):
		var img := root.get_texture().get_image()
		var fmt := img.get_format()
		# Raw linear float32 RGBA (an 8-bit sRGB image is decoded first), plus a PNG to look at.
		var f := img.duplicate()
		if fmt == Image.FORMAT_RGBA8 or fmt == Image.FORMAT_RGB8:
			f.srgb_to_linear()
		f.convert(Image.FORMAT_RGBAF)
		var fa := FileAccess.open(out + ".raw", FileAccess.WRITE)
		fa.store_buffer(f.get_data())
		fa.close()
		var png := img.duplicate()
		if fmt != Image.FORMAT_RGBA8 and fmt != Image.FORMAT_RGB8:
			png.linear_to_srgb()
		png.convert(Image.FORMAT_RGBA8)
		png.save_png(out + ".png")
		print("saved ", out, " format ", fmt, " size ", img.get_size())
		return true
	return false
