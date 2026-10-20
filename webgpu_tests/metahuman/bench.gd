extends Node3D

# Web perf scene: Ava at LOD 0 (head and torso framed), face and arms animated every frame.
# Every 4 s the rig is switched on or off; the console gets frame and process time per state.

var rig: MetaHumanRig
var skel: Skeleton3D
var controls: PackedStringArray
var t := 0.0
var phase_time := 0.0
var rig_on := true
var frames := 0
var process_us := 0.0
var frame_us := 0.0
var last_tick := 0
var arm_l := -1
var arm_r := -1
var fore_l := -1

func _ready() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.32, 0.34, 0.38)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.58, 0.65)
	env.ambient_light_energy = 0.6
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-25, 35, 0)
	key.light_energy = 2.2
	key.shadow_enabled = true
	add_child(key)
	var ava: Node3D = (load("res://Ava/head.dna") as PackedScene).instantiate()
	add_child(ava)
	rig = ava.find_child("MetaHumanRig", true, false)
	skel = ava.find_child("Skeleton3D", true, false)
	controls = rig.get_control_names()
	arm_l = skel.find_bone("upperarm_l")
	arm_r = skel.find_bone("upperarm_r")
	fore_l = skel.find_bone("lowerarm_l")
	var cam := Camera3D.new()
	cam.fov = 40
	add_child(cam)
	cam.look_at_from_position(Vector3(0.15, 1.5, 1.1), Vector3(0, 1.4, 0))
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	last_tick = Time.get_ticks_usec()
	print("MHBENCH controls=", controls.size(), " bones=", skel.get_bone_count())

func _process(delta: float) -> void:
	var start := Time.get_ticks_usec()
	t += delta
	# 40 expressions on independent sine waves.
	for i in mini(40, controls.size()):
		rig.set_control(controls[i * 6 % controls.size()], 0.5 + 0.5 * sin(t * (1.0 + i * 0.13) + i))
	skel.set_bone_pose_rotation(arm_l, skel.get_bone_rest(arm_l).basis.get_rotation_quaternion() * Quaternion(Vector3(0, 0, 1), sin(t) * 0.8))
	skel.set_bone_pose_rotation(arm_r, skel.get_bone_rest(arm_r).basis.get_rotation_quaternion() * Quaternion(Vector3(0, 1, 0), sin(t * 1.3) * 0.6))
	skel.set_bone_pose_rotation(fore_l, skel.get_bone_rest(fore_l).basis.get_rotation_quaternion() * Quaternion(Vector3(0, 0, 1), -1.0 - sin(t * 0.7)))
	var now := Time.get_ticks_usec()
	frame_us += now - last_tick
	last_tick = now
	process_us += Performance.get_monitor(Performance.TIME_PROCESS) * 1e6
	frames += 1
	phase_time += delta
	if phase_time >= 4.0:
		var gpu := RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
		var cpu := RenderingServer.viewport_get_measured_render_time_cpu(get_viewport().get_viewport_rid())
		print("MHBENCH rig=%s fps=%.1f frame_ms=%.2f process_ms=%.2f render_cpu_ms=%.2f render_gpu_ms=%.2f face_lod=%d body_lod=%d" % [
			"on" if rig_on else "off", frames / phase_time, frame_us / frames / 1000.0, process_us / frames / 1000.0, cpu, gpu,
			rig.get_current_face_lod(), rig.get_current_body_lod()])
		rig_on = not rig_on
		rig.active = rig_on
		phase_time = 0.0
		frames = 0
		process_us = 0.0
		frame_us = 0.0
