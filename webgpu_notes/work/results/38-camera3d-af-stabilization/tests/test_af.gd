extends SceneTree
# Autofocus tests: convergence (first order and hunting), single vs continuous, ray-cast points, collision mask,
# Practical DOF distances, editor pre/post-save restore. Prints PASS/FAIL lines and writes traces as JSON.

var fails := 0
var out := {}

func check(name: String, ok: bool, detail: String) -> void:
	print(("PASS " if ok else "FAIL ") + name + "  " + detail)
	if not ok:
		fails += 1

# Waits n sixtieths of a second of game time, whatever the frame rate.
func frames(n: int) -> void:
	var t := 0.0
	while t < n / 60.0 - 1e-9:
		await process_frame
		t += root.get_process_delta_time()

# A node that processes after every camera in the same frame (higher priority runs later) and calls cb(delta), so a
# trace samples exactly what the camera integrated, with the delta it used.
func recorder(cb: Callable) -> Node:
	var gd := GDScript.new()
	gd.source_code = "extends Node\nvar cb: Callable\nfunc _process(d: float) -> void:\n\tcb.call(d)\n"
	gd.reload()
	var n := Node.new()
	n.set_script(gd)
	n.set("cb", cb)
	n.process_priority = 1000
	root.add_child(n)
	return n

func make_cam(attr: CameraAttributes) -> Camera3D:
	var cam := Camera3D.new()
	cam.attributes = attr
	root.add_child(cam)
	return cam

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	# A: first-order convergence on a tracked node, Physical attributes.
	var phys := CameraAttributesPhysical.new()
	phys.frustum_focus_distance = 10.0
	var cam := make_cam(phys)
	var target := Node3D.new()
	root.add_child(target)
	target.position = Vector3(0.3, 0.2, -2.0)  # Depth along the axis is 2 m whatever the lateral offset.
	cam.autofocus_target = cam.get_path_to(target)
	cam.autofocus_time_constant = 0.2
	var acquired := []
	cam.focus_acquired.connect(func(d): acquired.append([Time.get_ticks_msec(), d]))
	var trace := []
	var times := []
	var t_acc := [0.0]
	var rec := recorder(func(d):
		t_acc[0] += d
		trace.append(1.0 / phys.frustum_focus_distance)
		times.append(t_acc[0]))
	cam.autofocus_mode = Camera3D.AUTOFOCUS_MODE_CONTINUOUS
	await frames(120)
	rec.queue_free()
	out["first_order"] = trace
	out["first_order_times"] = times
	# times[k] is the summed delta the camera integrated up to trace[k]. Exact first-order servo: remaining error =
	# e0 * exp(-T / tau) at every frame, at any frame rate.
	var e0 := 0.5 - 0.1
	var worst := 0.0
	var t95 := -1.0
	var r_tau := -1.0
	for k in trace.size():
		var r: float = (0.5 - trace[k]) / e0
		worst = maxf(worst, absf(r - exp(-times[k] / 0.2)))
		if r_tau < 0.0 and times[k] >= 0.2 - 1e-6:
			r_tau = r * exp((times[k] - 0.2) / 0.2)  # Brought back to exactly tau.
		if t95 < 0.0 and r < 0.05:
			t95 = times[k]
	check("AF first-order: error = e0 exp(-t / tau) at every frame", worst < 1e-4, "max deviation %s" % str(worst))
	check("AF first-order: remaining error after tau = 1/e", absf(r_tau - exp(-1.0)) < 1e-3, "ratio %.4f (e^-1 = %.4f)" % [r_tau, exp(-1.0)])
	check("AF first-order: 95% within 3 tau + one frame", t95 >= 0.6 - 1e-6 and t95 <= 0.6 + 1.0 / 30.0, "t95 %.4f s (3 tau = 0.6)" % t95)
	check("AF final distance", absf(phys.frustum_focus_distance - 2.0) < 0.01, "%.4f m" % phys.frustum_focus_distance)
	# Acquired = within a quarter of the depth of field's half width 1/H (CoC = 36x24 mm diagonal / 1500, as the engine).
	var coc := sqrt(36.0 * 36.0 + 24.0 * 24.0) / 1500.0
	var hyper := (35.0 + 35.0 * 35.0 / (16.0 * coc)) / 1000.0
	var tol := 0.25 / hyper
	check("AF focus_acquired emitted once, within DOF/4", acquired.size() == 1 and absf(1.0 / acquired[0][1] - 0.5) < tol, "%s (tolerance %.4f D, error %.4f D)" % [str(acquired), tol, absf(1.0 / acquired[0][1] - 0.5)])
	check("AF is_autofocus_acquired", cam.is_autofocus_acquired(), "")

	# Continuous follows a moving target.
	target.position.z = -5.0
	await frames(90)
	check("AF-C follows target to 5 m", absf(phys.frustum_focus_distance - 5.0) < 0.02, "%.4f m" % phys.frustum_focus_distance)
	check("AF-C re-acquires (2 signals)", acquired.size() == 2, str(acquired.size()))

	# Editor save hooks restore the user's value and re-apply.
	cam.notification(Node.NOTIFICATION_EDITOR_PRE_SAVE)
	var saved := phys.frustum_focus_distance
	cam.notification(Node.NOTIFICATION_EDITOR_POST_SAVE)
	check("AF pre-save restores baseline, post-save re-applies", absf(saved - 10.0) < 1e-4 and absf(phys.frustum_focus_distance - 5.0) < 0.02, "pre %.4f post %.4f" % [saved, phys.frustum_focus_distance])

	# User edit while AF-C runs becomes the new baseline.
	phys.frustum_focus_distance = 7.0
	await frames(1)
	cam.notification(Node.NOTIFICATION_EDITOR_PRE_SAVE)
	check("AF user edit becomes baseline", absf(phys.frustum_focus_distance - 7.0) < 1e-4, "%.4f" % phys.frustum_focus_distance)
	cam.notification(Node.NOTIFICATION_EDITOR_POST_SAVE)
	cam.queue_free()
	await frames(2)

	# B: hunting overshoot.
	for h in [0.1, 0.3, 0.5]:
		var ph := CameraAttributesPhysical.new()
		ph.frustum_focus_distance = 10.0
		var c2 := make_cam(ph)
		c2.autofocus_target = c2.get_path_to(target)
		target.position = Vector3(0, 0, -2)
		c2.autofocus_time_constant = 0.2
		c2.autofocus_hunting = h
		c2.autofocus_mode = Camera3D.AUTOFOCUS_MODE_CONTINUOUS
		var mx := 0.0
		var tr2 := []
		var peak := [0.0]
		var rec2 := recorder(func(_d):
			var dd := 1.0 / ph.frustum_focus_distance
			tr2.append(dd)
			peak[0] = maxf(peak[0], dd))
		await frames(180)
		rec2.queue_free()
		mx = peak[0]
		out["hunting_%s" % h] = tr2
		var os := (mx - 0.5) / 0.4
		# The peak sampled per frame can miss the true peak slightly: allow 0.02 absolute.
		check("AF hunting %.1f: overshoot fraction" % h, absf(os - h) < 0.02, "measured %.4f" % os)
		check("AF hunting %.1f: settles" % h, absf(ph.frustum_focus_distance - 2.0) < 0.02, "%.4f m" % ph.frustum_focus_distance)
		c2.queue_free()
		await frames(1)

	# C: single: does nothing until triggered, then locks.
	var ps := CameraAttributesPhysical.new()
	ps.frustum_focus_distance = 10.0
	var cs := make_cam(ps)
	cs.autofocus_target = cs.get_path_to(target)
	target.position = Vector3(0, 0, -2)
	cs.autofocus_time_constant = 0.1
	cs.autofocus_mode = Camera3D.AUTOFOCUS_MODE_SINGLE
	var got := []
	cs.focus_acquired.connect(func(d): got.append(d))
	await frames(30)
	check("AF-S idle before trigger", ps.frustum_focus_distance == 10.0, "%.4f" % ps.frustum_focus_distance)
	cs.autofocus_trigger()
	await frames(60)
	check("AF-S focuses after trigger", absf(ps.frustum_focus_distance - 2.0) < 0.05 and got.size() == 1, "%.4f m, signals %d" % [ps.frustum_focus_distance, got.size()])
	var locked := ps.frustum_focus_distance
	target.position.z = -6.0
	await frames(60)
	check("AF-S stays locked on the triggered distance when target moves", absf(ps.frustum_focus_distance - 2.0) < 0.01, "%.4f (target now 6 m)" % ps.frustum_focus_distance)
	cs.autofocus_trigger()
	await frames(60)
	check("AF-S re-trigger refocuses", absf(ps.frustum_focus_distance - 6.0) < 0.1 and got.size() == 2, "%.4f, signals %d" % [ps.frustum_focus_distance, got.size()])
	cs.queue_free()

	# D: ray-cast points.
	var wall := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(4, 4, 1)
	shape.shape = box
	wall.add_child(shape)
	wall.position = Vector3(0, 0, -4)  # Front face at 3.5 m.
	root.add_child(wall)
	var post := StaticBody3D.new()
	var shape2 := CollisionShape3D.new()
	var box2 := BoxShape3D.new()
	box2.size = Vector3(0.4, 0.4, 0.4)
	shape2.shape = box2
	post.add_child(shape2)
	root.add_child(post)
	var pr := CameraAttributesPhysical.new()
	pr.frustum_focus_distance = 10.0
	var cr := make_cam(pr)
	cr.autofocus_time_constant = 0.05
	await physics_frame
	await physics_frame
	# Put the post where screen point (0.8, 0.5) looks at 1.5 m.
	var vp := root.get_visible_rect().size
	var p := cr.project_position(Vector2(0.8, 0.5) * vp, 1.5)
	post.position = p - Vector3(0, 0, -0.2)  # Front face at depth 1.5.
	post.position.z = -1.7
	await physics_frame
	await physics_frame
	var failed := [0]
	cr.focus_failed.connect(func(): failed[0] += 1)
	cr.autofocus_mode = Camera3D.AUTOFOCUS_MODE_CONTINUOUS
	await frames(60)
	check("AF ray (center point) hits wall at 3.5 m", absf(pr.frustum_focus_distance - 3.5) < 0.01, "%.4f" % pr.frustum_focus_distance)
	cr.autofocus_points = PackedVector2Array([Vector2(0.5, 0.5), Vector2(0.8, 0.5)])
	await frames(60)
	check("AF nearest of points -> post at 1.5 m", absf(pr.frustum_focus_distance - 1.5) < 0.01, "%.4f" % pr.frustum_focus_distance)
	post.collision_layer = 2
	await physics_frame
	await frames(60)
	check("AF collision mask ignores layer 2", absf(pr.frustum_focus_distance - 3.5) < 0.01, "%.4f" % pr.frustum_focus_distance)
	wall.position.x = 100.0  # Nothing left in view.
	await physics_frame
	await frames(30)
	check("AF miss: holds focus, focus_failed once", absf(pr.frustum_focus_distance - 3.5) < 0.01 and failed[0] == 1, "%.4f, failed %d" % [pr.frustum_focus_distance, failed[0]])
	cr.queue_free()

	# E: Practical: the in-focus band moves around the focus distance with its diopter width.
	var pa := CameraAttributesPractical.new()
	pa.dof_blur_near_enabled = true
	pa.dof_blur_far_enabled = true
	pa.dof_blur_near_distance = 2.0
	pa.dof_blur_far_distance = 10.0
	pa.dof_blur_near_transition = 1.0
	pa.dof_blur_far_transition = 5.0
	var cp := make_cam(pa)
	target.position = Vector3(0, 0, -1.0)
	cp.autofocus_target = cp.get_path_to(target)
	cp.autofocus_time_constant = 0.05
	cp.autofocus_mode = Camera3D.AUTOFOCUS_MODE_CONTINUOUS
	await frames(90)
	# Reference: center 1 D, half width (1/2 - 1/10)/2 = 0.2 D -> near 1/1.2, far 1/0.8; transitions keep diopter widths.
	var en := 1.0 / 1.2
	var ef := 1.0 / 0.8
	var eft := 1.0 / (0.8 - (0.1 - 1.0 / 15.0)) - ef
	var ent := en - 1.0 / (1.2 + (1.0 / 1.0 - 0.5))
	var ok := absf(pa.dof_blur_near_distance - en) < 1e-3 and absf(pa.dof_blur_far_distance - ef) < 1e-3 and absf(pa.dof_blur_far_transition - eft) < 1e-3 and absf(pa.dof_blur_near_transition - ent) < 1e-3
	check("AF Practical moves DOF distances", ok, "near %.4f (%.4f) far %.4f (%.4f) near_tr %.4f (%.4f) far_tr %.4f (%.4f)" % [pa.dof_blur_near_distance, en, pa.dof_blur_far_distance, ef, pa.dof_blur_near_transition, ent, pa.dof_blur_far_transition, eft])
	check("AF Practical distance", absf(cp.get_autofocus_distance() - 1.0) < 0.01, "%.4f" % cp.get_autofocus_distance())
	target.position.z = -4.0  # 0.25 D: far limit at 0.05 D = 20 m.
	await frames(90)
	check("AF Practical far focus", absf(pa.dof_blur_far_distance - 20.0) < 0.1 and absf(pa.dof_blur_near_distance - 1.0 / 0.45) < 0.01, "near %.4f far %.4f" % [pa.dof_blur_near_distance, pa.dof_blur_far_distance])
	target.position.z = -50.0  # 0.02 D: past "hyperfocal", far blur pushed to infinity.
	await frames(120)
	check("AF Practical beyond hyperfocal", pa.dof_blur_far_distance >= 1.0e4, "far %.1f" % pa.dof_blur_far_distance)
	cp.notification(Node.NOTIFICATION_EDITOR_PRE_SAVE)
	check("AF Practical pre-save restores all four", pa.dof_blur_near_distance == 2.0 and pa.dof_blur_far_distance == 10.0 and pa.dof_blur_near_transition == 1.0 and pa.dof_blur_far_transition == 5.0, "")

	var f := FileAccess.open(OS.get_cmdline_user_args()[0] if OS.get_cmdline_user_args().size() > 0 else "/tmp/af.json", FileAccess.WRITE)
	f.store_string(JSON.stringify(out))
	f.close()
	print("AF TESTS DONE, failures: %d" % fails)
	quit(1 if fails > 0 else 0)
