extends RefCounted
# The grading cases of the color grading test (task 45): each applies one control (or a mix) to an Environment.
# Shared by grade_test.gd (native) and web_main.gd (web), mirrored by grade_ref.py's CASES.

static func linear_curve(points: Array) -> Curve:
	var c := Curve.new()
	for p in points:
		c.add_point(p, 0, 0, Curve.TANGENT_LINEAR, Curve.TANGENT_LINEAR)
	return c

# The user's own color correction LUTs: a 17^3 3D LUT (texel-center convention, as Godot samples it) and a 256x1 1D LUT.
static func user_lut_3d() -> ImageTexture3D:
	var n := 17
	var images: Array[Image] = []
	for b in n:
		var img := Image.create_empty(n, n, false, Image.FORMAT_RGBA8)
		for g in n:
			for r in n:
				var x := Vector3((r + 0.5) / n, (g + 0.5) / n, (b + 0.5) / n)
				# A channel rotation with a gamma: easy to tell from the grade.
				img.set_pixel(r, g, Color(pow(x.y, 0.8), pow(x.z, 1.2), x.x, 1.0))
		images.append(img)
	var t := ImageTexture3D.new()
	t.create(Image.FORMAT_RGBA8, n, n, n, false, images)
	return t

static func user_lut_1d() -> ImageTexture:
	var img := Image.create_empty(256, 1, false, Image.FORMAT_RGBA8)
	for i in 256:
		var x := (i + 0.5) / 256.0
		img.set_pixel(i, 0, Color(x * x, sqrt(x), 1.0 - x, 1.0))
	return ImageTexture.create_from_image(img)

# env is untyped so the base editor (no grading properties) can run the cases without grading.
static func apply(env, case_name: String) -> void:
	if case_name == "off":
		return
	if not case_name.ends_with("_only"):
		env.grading_enabled = true
	match case_name:
		"neutral":
			pass
		"filter":
			env.grading_color_filter = Color(1.0, 0.8, 0.55)
		"hue_shift":
			env.grading_hue_shift = 60.0
		"vibrance":
			env.grading_vibrance = 0.6
		"vibrance_neg":
			env.grading_vibrance = -0.7
		"vibrance_min":
			env.grading_vibrance = -1.0
		"lift":
			env.grading_lift = Color(0.1, 0.0, 0.2)
		"gamma":
			env.grading_gamma = Color(1.5, 1.0, 0.7)
		"gain":
			env.grading_gain = Color(1.2, 0.9, 1.0)
		"smh":
			env.grading_shadows = Color(0.6, 0.8, 1.4)
			env.grading_midtones = Color(1.1, 1.0, 0.9)
			env.grading_highlights = Color(1.3, 1.1, 0.7)
			env.grading_shadows_end = 0.25
			env.grading_highlights_start = 0.4
		"mixer":
			env.grading_channel_mixer_red = Vector3(0.8, 0.3, -0.1)
			env.grading_channel_mixer_green = Vector3(0.1, 0.7, 0.2)
			env.grading_channel_mixer_blue = Vector3(0.0, 0.4, 0.6)
		"split":
			env.grading_split_toning_shadows = Color(0.2, 0.4, 0.9)
			env.grading_split_toning_highlights = Color(0.9, 0.6, 0.2)
			env.grading_split_toning_balance = 0.1
		"curve_master":
			env.grading_curve_master = linear_curve([Vector2(0, 0.05), Vector2(0.3, 0.2), Vector2(0.7, 0.85), Vector2(1, 0.95)])
		"curve_rgb":
			env.grading_curve_red = linear_curve([Vector2(0, 0), Vector2(0.5, 0.65), Vector2(1, 1)])
			env.grading_curve_green = linear_curve([Vector2(0, 0.1), Vector2(1, 0.9)])
			env.grading_curve_blue = linear_curve([Vector2(0, 0), Vector2(0.5, 0.35), Vector2(1, 1)])
		"hue_vs_hue":
			env.grading_curve_hue_vs_hue = linear_curve([Vector2(0, 0.5), Vector2(0.33, 0.6), Vector2(0.66, 0.4), Vector2(1, 0.5)])
		"hue_vs_sat":
			env.grading_curve_hue_vs_sat = linear_curve([Vector2(0, 0.25), Vector2(0.5, 0.75), Vector2(1, 0.25)])
		"sat_vs_sat":
			env.grading_curve_sat_vs_sat = linear_curve([Vector2(0, 0.5), Vector2(1, 0.2)])
		"lum_vs_sat":
			env.grading_curve_lum_vs_sat = linear_curve([Vector2(0, 0.0), Vector2(0.5, 0.5), Vector2(1, 0.9)])
		"all", "all_user3d", "all_user1d", "all_adjust":
			env.grading_color_filter = Color(1.0, 0.95, 0.85)
			env.grading_hue_shift = -10.0
			env.grading_vibrance = 0.3
			env.grading_lift = Color(0.02, 0.0, 0.04)
			env.grading_gamma = Color(1.1, 1.0, 0.95)
			env.grading_gain = Color(1.05, 1.0, 0.95)
			env.grading_shadows = Color(0.9, 0.95, 1.1)
			env.grading_highlights = Color(1.1, 1.0, 0.9)
			env.grading_channel_mixer_red = Vector3(0.9, 0.1, 0.0)
			env.grading_split_toning_shadows = Color(0.4, 0.45, 0.6)
			env.grading_split_toning_highlights = Color(0.6, 0.55, 0.4)
			env.grading_curve_master = linear_curve([Vector2(0, 0.02), Vector2(0.25, 0.2), Vector2(0.75, 0.82), Vector2(1, 0.98)])
			env.grading_curve_sat_vs_sat = linear_curve([Vector2(0, 0.55), Vector2(1, 0.45)])
			if case_name == "all_user3d" or case_name == "all_adjust":
				env.adjustment_enabled = true
				env.adjustment_color_correction = user_lut_3d()
			if case_name == "all_user1d":
				env.adjustment_enabled = true
				env.adjustment_color_correction = user_lut_1d()
			if case_name == "all_adjust":
				env.adjustment_brightness = 1.1
				env.adjustment_contrast = 1.1
				env.adjustment_saturation = 0.8
		"user3d_only", "user1d_only":
			# No grading: the user's LUT alone, unchanged from before (bit-identical check).
			env.adjustment_enabled = true
			env.adjustment_color_correction = user_lut_3d() if case_name == "user3d_only" else user_lut_1d()
		_:
			push_error("unknown case " + case_name)
