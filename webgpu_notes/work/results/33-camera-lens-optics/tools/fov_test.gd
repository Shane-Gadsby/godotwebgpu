extends SceneTree
# Field of view per sensor preset, anamorphic and breathing; exposure with ND and T-stop loss. Prints JSON.
func _init():
	var out := {}
	var cam := Camera3D.new()
	root.add_child(cam)
	for fmt in range(7):
		var a := CameraAttributesPhysical.new()
		a.frustum_focal_length = 50.0
		a.sensor_format = fmt
		if fmt == CameraAttributesPhysical.SENSOR_FORMAT_CUSTOM:
			a.sensor_size = Vector2(30.0, 20.0)
		cam.attributes = a
		out["format_%d" % fmt] = {"size": [a.sensor_size.x, a.sensor_size.y], "fov": cam.fov, "keep": cam.keep_aspect}
	var an := CameraAttributesPhysical.new()
	an.sensor_format = CameraAttributesPhysical.SENSOR_FORMAT_SUPER_35
	an.frustum_focal_length = 50.0
	an.lens_anamorphic_squeeze = 2.0
	cam.attributes = an
	out["anamorphic_2x_s35_50mm"] = {"fov": cam.fov, "keep": cam.keep_aspect, "horizontal": an.is_fov_horizontal()}
	for b in [0.0, 1.0, -0.5]:
		var br := CameraAttributesPhysical.new()
		br.frustum_focal_length = 50.0
		br.frustum_focus_distance = 0.5
		br.lens_focus_breathing = b
		cam.attributes = br
		out["breathing_%s_50mm_at_0.5m" % b] = cam.fov
	print("FOVINFO ", JSON.stringify(out))
	quit()
