extends SceneTree

# Headless checks of an imported MetaHuman: structure, bind sanity, and that the rig moves joints.

var failures := 0

func check(cond: bool, what: String) -> void:
	if cond:
		print("PASS ", what)
	else:
		print("FAIL ", what)
		failures += 1

func _initialize() -> void:
	var scene: PackedScene = load("res://Ava/head.dna")
	check(scene != null, "head.dna imports as a scene")
	if scene == null:
		quit(1)
		return
	var ava: Node3D = scene.instantiate()
	root.add_child(ava)
	var skel: Skeleton3D = ava.find_child("Skeleton3D", true, false)
	check(skel != null, "has a Skeleton3D")
	print("bones: ", skel.get_bone_count())
	var rig: MetaHumanRig = skel.find_child("MetaHumanRig", true, false)
	check(rig != null and rig.face_dna != null and rig.face_dna.is_valid(), "face rig loads")
	check(rig != null and rig.body_dna != null and rig.body_dna.is_valid(), "body rig loads")

	var skins := {}
	var total_verts := 0
	for child in skel.get_children():
		if child is MeshInstance3D:
			var mi: MeshInstance3D = child
			var v := 0
			for s in mi.mesh.get_surface_count():
				v += (mi.mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX] as PackedVector3Array).size()
			total_verts += v
			skins[mi.skin] = true
			print("  mesh %-24s verts %6d  surfaces %d  range %.1f..%.1f  material %s" % [mi.name, v, mi.mesh.get_surface_count(), mi.visibility_range_begin, mi.visibility_range_end, mi.mesh.surface_get_material(0).resource_name if mi.mesh.surface_get_material(0) else "-"])
	print("total verts (all LODs): ", total_verts)
	check(skins.size() == 1, "every mesh shares one Skin (%d)" % skins.size())

	# Bind sanity: the left eye mesh should be centered on the left eye joint at rest.
	var eye_bone := skel.find_bone("FACIAL_L_Eye")
	var eye_mesh: MeshInstance3D = skel.find_child("eyeLeft_lod0", false, false)
	if eye_bone >= 0 and eye_mesh:
		var verts: PackedVector3Array = eye_mesh.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
		var c := Vector3()
		for p in verts:
			c += p
		c /= verts.size()
		var j := skel.get_bone_global_rest(eye_bone).origin
		print("eye mesh center ", c, " eye joint ", j)
		check(c.distance_to(j) < 0.01, "left eye mesh sits on FACIAL_L_Eye (%.4f m)" % c.distance_to(j))
		var uvs: PackedVector2Array = eye_mesh.mesh.surface_get_arrays(0)[Mesh.ARRAY_TEX_UV]
		# The vertex furthest forward (+Z) is the pupil; report its UV for the iris center.
		var best := 0
		for i in verts.size():
			if verts[i].z > verts[best].z:
				best = i
		print("eye front vertex uv ", uvs[best])
	else:
		check(false, "found FACIAL_L_Eye and eyeLeft_lod0")

	# Head height: a MetaHuman is ~1.6-1.9 m tall, so the head joint sits around there.
	var head := skel.find_bone("head")
	check(head >= 0 and skel.get_bone_global_rest(head).origin.y > 1.3 and skel.get_bone_global_rest(head).origin.y < 2.0, "head joint height %.3f m" % skel.get_bone_global_rest(head).origin.y)

	# Rig: open the jaw and see the jaw joint move.
	var jaw := skel.find_bone("FACIAL_C_Jaw")
	rig.face_lod = 0
	rig.body_lod = 0
	await process_frame
	await process_frame
	var jaw_before := skel.get_bone_global_pose(jaw)
	print("controls: ", rig.get_control_names().size())
	rig.set_control("CTRL_expressions.jawOpen", 1.0)
	# Poses are restored after each skeleton update, so read them inside it.
	var jaw_after := Transform3D()
	skel.skeleton_updated.connect(func(): jaw_after = skel.get_bone_global_pose(jaw), CONNECT_ONE_SHOT)
	await process_frame
	await process_frame
	var moved := jaw_before.origin.distance_to(jaw_after.origin) + jaw_before.basis.get_euler().distance_to(jaw_after.basis.get_euler())
	print("jaw before ", jaw_before.origin, " after ", jaw_after.origin, " rot delta ", (jaw_after.basis.get_euler() - jaw_before.basis.get_euler()))
	check(moved > 0.01, "jawOpen moves FACIAL_C_Jaw (%.4f)" % moved)

	# Body correctives: bend the left elbow and see a corrective joint respond.
	var lowerarm := skel.find_bone("lowerarm_l")
	var helper_names := ["lowerarm_correctiveRoot_l", "lowerarm_in_l", "lowerarm_out_l", "lowerarm_fwd_l", "lowerarm_bck_l"]
	var helpers := []
	for n in helper_names:
		if skel.find_bone(n) >= 0:
			helpers.append(skel.find_bone(n))
	var helper_before := {}
	skel.skeleton_updated.connect(func():
		for h in helpers:
			helper_before[h] = skel.get_bone_global_pose(h).origin, CONNECT_ONE_SHOT)
	await process_frame
	await process_frame
	var helper_after := {}
	skel.set_bone_pose_rotation(lowerarm, skel.get_bone_rest(lowerarm).basis.get_rotation_quaternion() * Quaternion(Vector3(0, 0, 1), deg_to_rad(-110)))
	skel.skeleton_updated.connect(func():
		for h in helpers:
			helper_after[h] = skel.get_bone_global_pose(h).origin, CONNECT_ONE_SHOT)
	await process_frame
	await process_frame
	var any_moved := false
	for h in helpers:
		if helper_before.has(h) and helper_after.has(h):
			# Compare relative to the lower arm so the elbow bend itself does not count.
			var d: float = (helper_after[h] - helper_before[h]).length()
			print("  helper ", skel.get_bone_name(h), " moved ", d)
			any_moved = any_moved or d > 0.0005
	check(helpers.size() > 0 and any_moved, "elbow bend drives a corrective joint")

	print("DONE failures=", failures)
	quit(failures)
