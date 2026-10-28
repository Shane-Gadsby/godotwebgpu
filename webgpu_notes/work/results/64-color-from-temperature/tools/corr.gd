extends SceneTree
# Prints Light3D.get_correlated_color() bit for bit over the temperature range (and outside it), for the
# pre/post bit-identical check. Run headless in a project with physical light units on.
func _init() -> void:
	var l := OmniLight3D.new()
	var out := PackedStringArray()
	var t := 500.0
	while t <= 20000.0:
		l.light_temperature = t
		out.append("%s %s" % [t, var_to_bytes(l.get_correlated_color()).hex_encode()])
		t += 13.7
	for x in [1000.0, 1000.5, 3200.0, 5600.0, 6500.0, 15000.0]:
		l.light_temperature = x
		out.append("%s %s" % [x, var_to_bytes(l.get_correlated_color()).hex_encode()])
	print("\n".join(out))
	l.free()
	quit()
