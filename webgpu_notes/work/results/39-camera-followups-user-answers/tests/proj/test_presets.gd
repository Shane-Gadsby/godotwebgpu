extends SceneTree
# Task 39: handheld shake presets. Checks that each preset writes the documented values (the table below is the class
# reference's), that editing a value switches to Custom and that a saved scene loads with its preset, then records 120 s
# of every preset's shake at a fixed 60 fps into JSON for presets_analyze.py. Usage: -s test_presets.gd -- out.json

const DOC := {
	# preset: [rotation amp (deg RMS), rotation freq (Hz), position amp (m RMS), position freq (Hz), breathing amp, breathing freq, stabilization]
	Camera3D.SHAKE_PRESET_HANDHELD: [Vector3(0.12, 0.12, 0.06), Vector3(3, 3, 2.5), Vector3(0.001, 0.001, 0.0005), Vector3(0.5, 0.5, 0.5), 0.1, 0.25, 0.0],
	Camera3D.SHAKE_PRESET_HANDHELD_STABILIZED: [Vector3(0.12, 0.12, 0.06), Vector3(3, 3, 2.5), Vector3(0.001, 0.001, 0.0005), Vector3(0.5, 0.5, 0.5), 0.1, 0.25, 0.8],
	Camera3D.SHAKE_PRESET_HANDHELD_WALKING: [Vector3(0.8, 0.6, 0.5), Vector3(2, 1, 1), Vector3(0.008, 0.012, 0.005), Vector3(1, 2, 2), 0.15, 0.4, 0.0],
	Camera3D.SHAKE_PRESET_RUNNING: [Vector3(2, 1.5, 1.5), Vector3(2.8, 1.4, 1.4), Vector3(0.015, 0.03, 0.01), Vector3(1.4, 2.8, 2.8), 0.3, 0.7, 0.0],
	Camera3D.SHAKE_PRESET_SHOULDER_RIG: [Vector3(0.15, 0.2, 0.2), Vector3(1.2, 1, 0.8), Vector3(0.003, 0.003, 0.002), Vector3(0.8, 0.8, 0.8), 0.12, 0.25, 0.0],
	Camera3D.SHAKE_PRESET_GIMBAL: [Vector3(0.03, 0.03, 0.02), Vector3(0.5, 0.5, 0.5), Vector3(0.004, 0.006, 0.003), Vector3(1, 2, 2), 0.0, 0.25, 0.0],
	Camera3D.SHAKE_PRESET_TRIPOD_WIND: [Vector3(0.03, 0.03, 0.01), Vector3(10, 10, 10), Vector3(0.0002, 0.0002, 0.0001), Vector3(10, 10, 10), 0.0, 0.25, 0.0],
	Camera3D.SHAKE_PRESET_VEHICLE_MOUNT: [Vector3(0.3, 0.1, 0.3), Vector3(1.2, 8, 1), Vector3(0.003, 0.01, 0.003), Vector3(1, 1.2, 1.2), 0.0, 0.25, 0.0],
}
const SETTERS := ["shake_rotation_amplitude", "shake_rotation_frequency", "shake_position_amplitude", "shake_position_frequency", "shake_breathing_amplitude", "shake_breathing_frequency", "shake_stabilization"]

var fails := 0

func check(name: String, ok: bool, detail: String = "") -> void:
	print(("PASS " if ok else "FAIL ") + name + "  " + detail)
	if not ok:
		fails += 1

func values(c: Camera3D) -> Array:
	return [c.shake_rotation_amplitude, c.shake_rotation_frequency, c.shake_position_amplitude, c.shake_position_frequency, c.shake_breathing_amplitude, c.shake_breathing_frequency, c.shake_stabilization]

func same(a: Array, b: Array) -> bool:
	for i in a.size():
		if a[i] is Vector3:
			if not (a[i] as Vector3).is_equal_approx(b[i]):
				return false
		elif not is_equal_approx(float(a[i]), float(b[i])):
			return false
	return true

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	var fresh := Camera3D.new()
	check("default preset is Custom, shake off", fresh.shake_preset == Camera3D.SHAKE_PRESET_CUSTOM and not fresh.shake_enabled)
	check("default values unchanged (task 38's)", same(values(fresh), [Vector3(0.3, 0.3, 0.15), Vector3(2, 2, 1.5), Vector3(0.002, 0.002, 0.001), Vector3(1.5, 1.5, 1.5), 0.15, 0.25, 0.0]) and fresh.shake_scale_with_focal_length)
	fresh.free()

	var cams := {}
	for p in DOC:
		var c := Camera3D.new()
		c.shake_scale_with_focal_length = false
		c.shake_preset = p
		check("preset %d writes its documented values" % p, same(values(c), DOC[p]) and c.shake_scale_with_focal_length, str(values(c)))
		check("preset %d stays selected after writing" % p, c.shake_preset == p)
		c.shake_seed = 3
		check("preset %d: changing the seed keeps the preset" % p, c.shake_preset == p)
		cams[p] = c

	# Editing any value flips to Custom and keeps the values; writing a value equal to the preset's keeps it.
	for i in SETTERS.size():
		var c := Camera3D.new()
		c.shake_preset = Camera3D.SHAKE_PRESET_HANDHELD_WALKING
		c.set(SETTERS[i], c.get(SETTERS[i]))
		var kept := c.shake_preset == Camera3D.SHAKE_PRESET_HANDHELD_WALKING
		var v = c.get(SETTERS[i])
		var edited = v * 0.5 if not (v is float and v == 0.0) else 0.5
		c.set(SETTERS[i], edited)
		check("editing %s switches to Custom (same value keeps the preset)" % SETTERS[i], kept and c.shake_preset == Camera3D.SHAKE_PRESET_CUSTOM and c.get(SETTERS[i]) == edited)
		c.free()
	var c2 := Camera3D.new()
	c2.shake_preset = Camera3D.SHAKE_PRESET_RUNNING
	c2.shake_scale_with_focal_length = false
	check("editing shake_scale_with_focal_length switches to Custom", c2.shake_preset == Camera3D.SHAKE_PRESET_CUSTOM)
	c2.shake_preset = Camera3D.SHAKE_PRESET_CUSTOM
	check("choosing Custom changes nothing", same(values(c2), DOC[Camera3D.SHAKE_PRESET_RUNNING]) and not c2.shake_scale_with_focal_length)
	c2.free()

	# Save and load: the preset and its values come back, and loading does not flip it to Custom.
	for p in [Camera3D.SHAKE_PRESET_GIMBAL, Camera3D.SHAKE_PRESET_TRIPOD_WIND]:
		var holder := Node3D.new()
		var c := Camera3D.new()
		c.name = "Cam"
		c.shake_enabled = true
		c.shake_preset = p
		holder.add_child(c)
		c.owner = holder
		var ps := PackedScene.new()
		ps.pack(holder)
		var path := "user://preset_%d.tscn" % p
		ResourceSaver.save(ps, path)
		var loaded: Node = (ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene).instantiate()
		var lc: Camera3D = loaded.get_node("Cam")
		check("preset %d survives save and load" % p, lc.shake_preset == p and same(values(lc), DOC[p]), FileAccess.get_file_as_string(path).replace("\n", " | "))
		loaded.free()
		holder.free()
	var c3 := Camera3D.new()
	c3.shake_preset = Camera3D.SHAKE_PRESET_SHOULDER_RIG
	c3.shake_breathing_amplitude = 0.5
	var h3 := Node3D.new()
	c3.name = "Cam"
	h3.add_child(c3)
	c3.owner = h3
	var ps3 := PackedScene.new()
	ps3.pack(h3)
	ResourceSaver.save(ps3, "user://custom.tscn")
	var lh3: Node = (ResourceLoader.load("user://custom.tscn", "", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene).instantiate()
	var l3: Camera3D = lh3.get_node("Cam")
	check("an edited preset loads as Custom with the edited values", l3.shake_preset == Camera3D.SHAKE_PRESET_CUSTOM and is_equal_approx(l3.shake_breathing_amplitude, 0.5) and l3.shake_rotation_amplitude.is_equal_approx(Vector3(0.15, 0.2, 0.2)))
	lh3.free()
	h3.free()

	# Record.
	for p in cams:
		root.add_child(cams[p])
		cams[p].shake_enabled = true
	var data := {}
	for p in cams:
		data[str(p)] = []
	var n := 120 * 60
	for i in n:
		await process_frame
		for p in cams:
			var o: Transform3D = cams[p].get_shake_offset()
			var e := o.basis.get_euler()
			data[str(p)].append([rad_to_deg(e.x), rad_to_deg(e.y), rad_to_deg(e.z), o.origin.x, o.origin.y, o.origin.z])
	var doc := {}
	for p in DOC:
		var d: Array = DOC[p]
		doc[str(p)] = [[d[0].x, d[0].y, d[0].z], [d[1].x, d[1].y, d[1].z], [d[2].x, d[2].y, d[2].z], [d[3].x, d[3].y, d[3].z], d[4], d[5], d[6]]
	var f := FileAccess.open(OS.get_cmdline_user_args()[0], FileAccess.WRITE)
	f.store_string(JSON.stringify({"fps": 60, "doc": doc, "data": data}))
	f.close()
	print("PRESET TESTS DONE, failures: %d" % fails)
	quit(1 if fails > 0 else 0)
