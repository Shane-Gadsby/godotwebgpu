extends SceneTree
# Worker-63 A/B: camera-sim's Practical DOF (update_dof) against CameraAttributesPhysical with dof_physical_blur_size.
# Env: OUT=dir, ARM=practical|physical, SHAPE=hexagon|circle. Renders f/8 u=5, f/1.4 u=5, f/1.4 u=2.5 at 1244x700.
var out_dir := ""
var arm := ""
var frame := 0
var configs := [[8.0, 5.0], [1.4, 5.0], [1.4, 2.5]]
var idx := -1
var next_at := 150
var main: Node

func _initialize():
	out_dir = OS.get_environment("OUT")
	arm = OS.get_environment("ARM")
	DirAccess.make_dir_recursive_absolute(out_dir)
	change_scene_to_file("res://scenes/camera_studio.tscn")

func n(name: String) -> Node:
	return current_scene.find_child(name, true, false)

func setup():
	main = current_scene
	var shape := OS.get_environment("SHAPE")
	RenderingServer.camera_attributes_set_dof_blur_bokeh_shape(RenderingServer.DOF_BOKEH_CIRCLE if shape == "circle" else RenderingServer.DOF_BOKEH_HEXAGON)
	RenderingServer.camera_attributes_set_dof_blur_quality(RenderingServer.DOF_BLUR_QUALITY_HIGH, false) # jitter off: deterministic
	main._auto_candidates.clear()
	var vp: SubViewport = n("SubViewport")
	vp.size = Vector2i(1244, 700)
	var idle = main.get("idle_animation")
	if idle:
		idle.set_process(false)
	var cam: Camera3D = main.camera
	cam.autofocus_mode = Camera3D.AUTOFOCUS_MODE_MANUAL
	main.settings.autofocus_enabled = false
	cam.attributes.grain_enabled = false
	if arm == "physical":
		var pr: CameraAttributes = cam.attributes
		var ph := CameraAttributesPhysical.new()
		var names := {}
		for p in ph.get_property_list():
			names[p.name] = true
		for p in pr.get_property_list():
			if (p.usage & PROPERTY_USAGE_STORAGE) and names.has(p.name) and not p.name.begins_with("resource_") and p.name != "script":
				ph.set(p.name, pr.get(p.name))
		ph.sensor_size = Vector2(36.0, 36.0 * 9.0 / 16.0)
		ph.frustum_near = cam.near
		ph.frustum_far = cam.far
		ph.dof_physical_blur_size = true
		cam.attributes = ph

func apply(c):
	var s = main.settings
	s.aperture = c[0]
	s.manual_focus_distance = c[1]
	s.focus_distance = c[1]
	var cam: Camera3D = main.camera
	if arm == "physical":
		var ph: CameraAttributesPhysical = cam.attributes
		ph.frustum_focal_length = s.focal_length
		ph.exposure_aperture = s.aperture
		ph.frustum_focus_distance = c[1]

func _process(_d):
	frame += 1
	if frame == 100:
		setup()
	if frame < next_at:
		return false
	if idx >= 0:
		var vp: SubViewport = n("SubViewport")
		var tag := "%s_f%s_u%s" % [arm, str(configs[idx][0]), str(configs[idx][1])]
		vp.get_texture().get_image().save_png(out_dir + "/" + tag + ".png")
		var cam: Camera3D = main.camera
		var head: Node3D = current_scene.get_node("Subject/Marker3D")
		var p := cam.unproject_position(head.global_position)
		var f := FileAccess.open(out_dir + "/" + tag + ".txt", FileAccess.WRITE)
		f.store_line("%f %f %d %d fov=%f keep=%d dist=%f" % [p.x, p.y, vp.size.x, vp.size.y, cam.fov, cam.keep_aspect, cam.global_position.distance_to(head.global_position)])
		print("SHOT ", tag, " head ", p, " fov ", cam.fov)
	idx += 1
	if idx >= configs.size():
		return true
	apply(configs[idx])
	next_at = frame + 60
	return false
