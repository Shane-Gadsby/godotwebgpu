extends SceneTree
# Task 65 headless checks: property visibility, get_correlated_color() following the opt-in, save/load.
func usage(o: Object, name: String) -> int:
	for p in o.get_property_list():
		if p.name == name:
			return p.usage
	return -1

func _init() -> void:
	var plu: bool = ProjectSettings.get_setting("rendering/lights_and_shadows/use_physical_light_units")
	var ok := true
	for l: Light3D in [OmniLight3D.new(), SpotLight3D.new(), DirectionalLight3D.new()]:
		var vis_off := usage(l, "light_temperature") & PROPERTY_USAGE_EDITOR != 0
		l.light_temperature_enabled = true
		var vis_on := usage(l, "light_temperature") & PROPERTY_USAGE_EDITOR != 0
		var en_vis := usage(l, "light_temperature_enabled") & PROPERTY_USAGE_EDITOR != 0
		l.light_temperature = 3200.0
		var c := l.get_correlated_color().srgb_to_linear()
		var y := 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
		var e := Color.from_temperature(3200.0)
		var match_ := c.is_equal_approx(e)
		l.light_temperature_enabled = false
		var off := l.get_correlated_color()
		print("[P] plu=%s %s: temp visible off/on %s/%s, enabled visible %s, Y(on, 3200 K) %.6f, == from_temperature %s, correlated after off %s" % [plu, l.get_class(), vis_off, vis_on, en_vis, y, match_, off])
		ok = ok and vis_on and en_vis and (vis_off == plu) and absf(y - 1.0) < 1e-5 and match_
		ok = ok and (off == (Color(1, 1, 1) if not plu else off))
		# Save/load round trip.
		l.light_temperature_enabled = true
		l.light_temperature = 4300.0
		var ps := PackedScene.new()
		ps.pack(l)
		ResourceSaver.save(ps, "user://l.tscn")
		var l2: Light3D = (load("user://l.tscn") as PackedScene).instantiate()
		ok = ok and l2.light_temperature_enabled and l2.light_temperature == 4300.0 and l2.get_correlated_color() == l.get_correlated_color()
		print("[P]   reload: enabled %s temp %s correlated equal %s" % [l2.light_temperature_enabled, l2.light_temperature, l2.get_correlated_color() == l.get_correlated_color()])
		l2.free()
		l.free()
	print("[P] RESULT ", "PASS" if ok else "FAIL")
	quit()
