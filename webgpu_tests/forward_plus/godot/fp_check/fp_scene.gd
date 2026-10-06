extends Node3D
##
## Forward+ feature matrix fixture.
##
## Builds one scene containing content every Forward+ feature can act on, then
## reconfigures itself on demand so the harness can capture the same frame with
## a feature off and on. The assertion is differential -- "this feature changes
## the frame" -- because the failure this suite exists to catch is a feature
## silently doing nothing, which is what a shader that fails to convert looks
## like from the outside: no error, no crash, just a missing effect.
##
## Protocol (run_forward_plus.mjs drives it):
##   location.hash = "#fp=<feature_id>:<0|1>"
## The script polls the hash, applies the config, waits the feature's settle
## frames, then prints:
##   [FP] READY <feature_id> <0|1>
## and on an unknown id:
##   [FP] UNKNOWN <feature_id>
## so a feature added to features.mjs without a matching arm here fails loudly
## rather than silently comparing a frame against itself.
##
## Everything is built procedurally. That is deliberate: a .tscn with baked
## resources cannot express a skinned mesh, blend shapes or a MultiMesh without
## binary assets, and binary fixtures are the kind of thing that rots silently
## across upstream syncs.

## Every feature this fixture can drive, published at boot as
##   [FP] FEATURES a,b,c
## run_forward_plus.mjs asserts this list and features.mjs agree exactly, in
## both directions. That is the mechanism behind the "a new feature needs a
## test" rule in this tier's README: add an arm here without a features.mjs
## entry (or the reverse) and the suite fails naming the offender, rather than
## quietly not testing it.
const FEATURES := [
	"ambient_light", "reflected_light", "sky", "tonemap", "tonemap_exposure",
	"adjustments", "color_correction", "fog", "fog_height", "fog_aerial_perspective",
	"volumetric_fog", "glow", "glow_map", "ssr", "ssao", "ssil", "sdfgi",
	"directional_light", "directional_shadow", "directional_shadow_splits",
	"omni_light", "omni_shadow", "spot_light", "spot_shadow", "light_projector",
	"soft_shadows", "shadow_blur", "contact_shadows", "light_negative", "light_specular",
	"reflection_probe", "voxel_gi", "decal",
	"normal_map", "emission", "rim", "clearcoat", "anisotropy",
	"subsurface_scattering", "backlight", "refraction", "heightmap", "triplanar",
	"transparency", "alpha_scissor", "alpha_hash", "proximity_fade", "distance_fade",
	# skeletal_animation and particle_trails have _apply() arms below but are
	# deliberately NOT listed: the fixture cannot drive either to a measurable
	# result yet, so they live in features.mjs's UNCOVERED list instead. Put
	# them back here the moment the fixture can actually move them.
	"multimesh", "blend_shapes", "mesh_lod", "visibility_range",
	"gpu_particles", "particle_collision", "particle_attractor",
	"msaa", "taa", "fxaa", "debanding", "scaling_3d", "fsr2",
	"dof_far", "dof_near", "auto_exposure",
]

const SETTLE_DEFAULT := 4
# Features whose result accumulates over frames. Undershooting these produces a
# flaky "feature does nothing" failure, which is worse than a slow test.
const SETTLE_OVERRIDE := {
	"sdfgi": 45,
	"volumetric_fog": 30,
	"taa": 24,
	"auto_exposure": 40,
	"ssr": 10,
	"ssil": 10,
	"gpu_particles": 40,
	"particle_trails": 50,
	"particle_collision": 50,
	"particle_attractor": 50,
	"reflection_probe": 12,
	"voxel_gi": 20,
	"fsr2": 16,
}

var env: Environment
var cam: Camera3D
var cam_attrs: CameraAttributesPractical
var world_env: WorldEnvironment

var dir_light: DirectionalLight3D
var omni_light: OmniLight3D
var spot_light: SpotLight3D
var probe: ReflectionProbe
var voxel_gi: VoxelGI
var decal: Decal
var particles: GPUParticles3D
var particle_collider: GPUParticlesCollisionSphere3D
var particle_attractor: GPUParticlesAttractorSphere3D
var multimesh_inst: MultiMeshInstance3D
var skinned: MeshInstance3D
var skeleton: Skeleton3D
var blendshape_inst: MeshInstance3D
var lod_inst: MeshInstance3D
var fade_inst: MeshInstance3D

var mat_floor: StandardMaterial3D
var mat_probe: StandardMaterial3D      # the material feature tests mutate this one
var mat_glass: StandardMaterial3D
var mat_emissive: StandardMaterial3D

var tex_checker: ImageTexture
var tex_normal: ImageTexture
var tex_gradient: ImageTexture
var tex_lut: ImageTexture

var _last_hash := ""
var _settle := -1
var _pending_id := ""
var _pending_on := false


func _ready() -> void:
	_build_textures()
	_build_environment()
	_build_geometry()
	_build_lights()
	_build_gi()
	_build_extras()
	# Native runs (no browser) take the feature from the command line so the
	# same fixture can be driven against Vulkan for a cross-backend reference.
	print("[FP] FEATURES %s" % ",".join(FEATURES))
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--fp-matrix="):
			_run_native_matrix(a.substr(12))
			return
		if a.begins_with("--fp="):
			_request(a.substr(5))
	print("[FP] BOOTED")


## Native reference sweep: render every feature off and on through whatever
## backend the engine was started with (normally Vulkan) and write the frames to
## disk, so run_forward_plus.mjs's WebGPU deltas can be compared against a known
## good renderer on the identical scene.
##
## This is what makes a failing feature diagnosable instead of ambiguous. A
## WebGPU delta near zero means nothing on its own -- the scene might simply not
## exercise the feature. Against a Vulkan reference it splits cleanly: both near
## zero is a fixture that needs improving, Vulkan large and WebGPU zero is a port
## bug. Thresholds in features.mjs are set from the Vulkan column for that
## reason, not from whatever WebGPU happened to produce.
##
##   godot --path . --rendering-driver vulkan -- --fp-matrix=/tmp/fpref
func _run_native_matrix(dir: String) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	for id in FEATURES:
		for on in [false, true]:
			_reset()
			if not _apply(id, on):
				print("[FP] UNKNOWN %s" % id)
				continue
			var settle := int(SETTLE_OVERRIDE.get(id, SETTLE_DEFAULT))
			for i in settle:
				await get_tree().process_frame
			await RenderingServer.frame_post_draw
			var img := get_viewport().get_texture().get_image()
			img.save_png("%s/%s_%d.png" % [dir, id, 1 if on else 0])
		print("[FP] NATIVE %s" % id)
	print("[FP] NATIVE-DONE %d" % FEATURES.size())
	get_tree().quit()


## Native validation path: apply every feature on and off in one run, so a typo
## in an _apply() arm surfaces as an engine error here rather than as a mystery
## "no visible difference" in the browser matrix hours later.
##   godot --path . --rendering-driver vulkan -- --fp=all
func _exercise_all() -> void:
	for id in FEATURES:
		for on in [true, false]:
			_reset()
			if not _apply(id, on):
				print("[FP] UNKNOWN %s" % id)
	print("[FP] EXERCISED %d" % FEATURES.size())


# ──────────────────────────────────────────────────────────────────────────────
# Content
# ──────────────────────────────────────────────────────────────────────────────

func _build_textures() -> void:
	# Checker albedo: high-frequency detail so AA, scaling and sharpening have
	# something to act on. A flat colour would make those tests unmeasurable.
	var img := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var c := Color(0.9, 0.9, 0.9) if ((x / 4) + (y / 4)) % 2 == 0 else Color(0.12, 0.12, 0.14)
			img.set_pixel(x, y, c)
	tex_checker = ImageTexture.create_from_image(img)

	# Bumpy normal map (tangent space).
	var nimg := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var nx := sin(float(x) * 0.6) * 0.5
			var ny := cos(float(y) * 0.6) * 0.5
			nimg.set_pixel(x, y, Color(nx * 0.5 + 0.5, ny * 0.5 + 0.5, 1.0))
	tex_normal = ImageTexture.create_from_image(nimg)

	# Radial gradient, used as a light projector, a decal albedo and a glow map.
	var gimg := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var d := Vector2(x - 32, y - 32).length() / 32.0
			var v: float = clampf(1.0 - d, 0.0, 1.0)
			gimg.set_pixel(x, y, Color(v, v * 0.6, 0.2, v))
	tex_gradient = ImageTexture.create_from_image(gimg)

	# 1D colour-correction ramp that visibly shifts hue, so the adjustment test
	# cannot pass on a neutral LUT that changes nothing.
	var limg := Image.create(256, 1, false, Image.FORMAT_RGBA8)
	for x in 256:
		var t := float(x) / 255.0
		limg.set_pixel(x, 0, Color(pow(t, 0.6), t, pow(t, 1.6)))
	tex_lut = ImageTexture.create_from_image(limg)


func _build_environment() -> void:
	env = Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	var pm := ProceduralSkyMaterial.new()
	pm.sky_top_color = Color(0.25, 0.4, 0.7)
	pm.sky_horizon_color = Color(0.5, 0.55, 0.6)
	pm.ground_bottom_color = Color(0.12, 0.1, 0.09)
	pm.ground_horizon_color = Color(0.35, 0.33, 0.3)
	sky.sky_material = pm
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 1.0
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC

	world_env = WorldEnvironment.new()
	world_env.environment = env
	add_child(world_env)

	cam_attrs = CameraAttributesPractical.new()
	cam = Camera3D.new()
	cam.transform = Transform3D(Basis(), Vector3(0, 2.6, 7.5)).looking_at(Vector3(0, 1.0, 0), Vector3.UP)
	cam.fov = 55.0
	cam.attributes = cam_attrs
	cam.current = true
	add_child(cam)


func _mesh_instance(mesh: Mesh, mat: Material, xform: Transform3D) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.transform = xform
	add_child(mi)
	return mi


func _build_geometry() -> void:
	# Floor: smooth and metallic enough for SSR and reflection probes to show.
	mat_floor = StandardMaterial3D.new()
	mat_floor.albedo_color = Color(0.35, 0.36, 0.4)
	mat_floor.metallic = 0.6
	mat_floor.roughness = 0.18
	var plane := PlaneMesh.new()
	plane.size = Vector2(40, 40)
	_mesh_instance(plane, mat_floor, Transform3D())

	# The material-feature subject. Large and centre-frame so a material change
	# moves a big share of the pixels.
	mat_probe = StandardMaterial3D.new()
	mat_probe.albedo_color = Color(0.8, 0.75, 0.7)
	mat_probe.albedo_texture = tex_checker
	mat_probe.roughness = 0.45
	var sph := SphereMesh.new()
	sph.radius = 1.1
	sph.height = 2.2
	_mesh_instance(sph, mat_probe, Transform3D(Basis(), Vector3(0, 1.2, 0)))

	# Tight concave corners: this is what SSAO/SSIL/contact shadows act on.
	var box := BoxMesh.new()
	box.size = Vector3(1.4, 2.4, 1.4)
	var mat_box := StandardMaterial3D.new()
	mat_box.albedo_color = Color(0.7, 0.7, 0.72)
	mat_box.roughness = 0.8
	_mesh_instance(box, mat_box, Transform3D(Basis(), Vector3(-2.6, 1.2, -0.6)))
	_mesh_instance(box, mat_box, Transform3D(Basis(), Vector3(2.6, 1.2, -0.6)))
	var slab := BoxMesh.new()
	slab.size = Vector3(7.2, 0.25, 1.2)
	_mesh_instance(slab, mat_box, Transform3D(Basis(), Vector3(0, 2.5, -0.6)))

	# Bright emitter for glow / HDR threshold work.
	mat_emissive = StandardMaterial3D.new()
	mat_emissive.albedo_color = Color(1, 0.85, 0.5)
	mat_emissive.emission_enabled = true
	mat_emissive.emission = Color(1.0, 0.8, 0.35)
	mat_emissive.emission_energy_multiplier = 8.0
	var esph := SphereMesh.new()
	esph.radius = 0.5
	esph.height = 1.0
	_mesh_instance(esph, mat_emissive, Transform3D(Basis(), Vector3(-1.6, 3.1, 1.4)))

	# Transparent subject, parked out of the way until a test enables it.
	mat_glass = StandardMaterial3D.new()
	mat_glass.albedo_color = Color(0.6, 0.8, 1.0, 0.45)
	mat_glass.roughness = 0.1
	var gsph := SphereMesh.new()
	gsph.radius = 0.9
	gsph.height = 1.8
	var glass := _mesh_instance(gsph, mat_glass, Transform3D(Basis(), Vector3(1.7, 1.1, 2.2)))
	glass.visible = false
	glass.name = "Glass"

	# LOD and distance-fade subjects.
	var lod_mesh := SphereMesh.new()
	lod_mesh.radius = 0.7
	lod_mesh.height = 1.4
	var im := ImporterMesh.new()
	im.add_surface(Mesh.PRIMITIVE_TRIANGLES, lod_mesh.get_mesh_arrays())
	im.generate_lods(25.0, 60.0, [])
	lod_inst = _mesh_instance(im.get_mesh(), mat_box, Transform3D(Basis(), Vector3(-4.6, 0.8, 2.0)))
	fade_inst = _mesh_instance(lod_mesh, mat_box, Transform3D(Basis(), Vector3(4.6, 0.8, 1.6)))


func _build_lights() -> void:
	dir_light = DirectionalLight3D.new()
	dir_light.transform = Transform3D(Basis(), Vector3(0, 8, 0)).looking_at(Vector3(1.4, 0, -1.0), Vector3.UP)
	dir_light.light_energy = 1.6
	dir_light.shadow_enabled = true
	add_child(dir_light)

	omni_light = OmniLight3D.new()
	omni_light.transform = Transform3D(Basis(), Vector3(2.4, 2.6, 2.6))
	omni_light.light_color = Color(0.5, 0.8, 1.0)
	omni_light.light_energy = 6.0
	omni_light.omni_range = 12.0
	omni_light.shadow_enabled = false
	omni_light.visible = false
	add_child(omni_light)

	spot_light = SpotLight3D.new()
	spot_light.transform = Transform3D(Basis(), Vector3(-2.6, 5.0, 2.2)).looking_at(Vector3(-0.6, 0.6, -0.4), Vector3.UP)
	spot_light.light_color = Color(1.0, 0.75, 0.5)
	spot_light.light_energy = 12.0
	spot_light.spot_range = 18.0
	spot_light.spot_angle = 30.0
	spot_light.shadow_enabled = false
	spot_light.visible = false
	add_child(spot_light)


func _build_gi() -> void:
	probe = ReflectionProbe.new()
	probe.transform = Transform3D(Basis(), Vector3(0, 2.0, 0))
	probe.size = Vector3(16, 8, 16)
	probe.intensity = 2.0
	probe.visible = false
	add_child(probe)

	# VoxelGI needs baked data. bake() is callable at runtime but is slow and
	# needs the scene already in the tree, so it is deferred to first use.
	voxel_gi = VoxelGI.new()
	voxel_gi.size = Vector3(16, 8, 16)
	voxel_gi.transform = Transform3D(Basis(), Vector3(0, 3.0, 0))
	voxel_gi.visible = false
	add_child(voxel_gi)


func _build_extras() -> void:
	decal = Decal.new()
	decal.size = Vector3(4, 3, 4)
	decal.texture_albedo = tex_gradient
	decal.transform = Transform3D(Basis(), Vector3(0.6, 0.6, 1.6))
	decal.visible = false
	add_child(decal)

	# MultiMesh: exercises the instanced draw path rather than per-instance draws.
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	var bm := BoxMesh.new()
	bm.size = Vector3(0.35, 0.35, 0.35)
	mm.mesh = bm
	mm.instance_count = 64
	for i in 64:
		var a := float(i) * 0.28
		mm.set_instance_transform(i, Transform3D(Basis(), Vector3(cos(a) * 4.2, 0.3 + float(i % 7) * 0.22, sin(a) * 2.4 - 1.0)))
	multimesh_inst = MultiMeshInstance3D.new()
	multimesh_inst.multimesh = mm
	multimesh_inst.material_override = mat_floor
	multimesh_inst.visible = false
	add_child(multimesh_inst)

	_build_skinned()
	_build_blendshape()

	particles = GPUParticles3D.new()
	var pmat := ParticleProcessMaterial.new()
	pmat.direction = Vector3(0, 1, 0)
	pmat.spread = 25.0
	pmat.initial_velocity_min = 2.5
	pmat.initial_velocity_max = 4.0
	pmat.gravity = Vector3(0, -2.0, 0)
	pmat.scale_min = 0.5
	pmat.scale_max = 1.0
	# Without this the GPUParticlesCollision* nodes do nothing at all -- the
	# collider is consulted only when the process material opts in.
	pmat.collision_mode = ParticleProcessMaterial.COLLISION_RIGID
	var pdraw := SphereMesh.new()
	pdraw.radius = 0.12
	pdraw.height = 0.24
	particles.process_material = pmat
	particles.draw_pass_1 = pdraw
	particles.material_override = mat_emissive
	particles.amount = 192
	particles.lifetime = 2.5
	particles.use_fixed_seed = true
	particles.seed = 20261006
	particles.fixed_fps = 30
	particles.interpolate = false
	# Kept small on purpose: preprocess re-runs on every _reset(), so a large
	# value multiplies across the whole matrix (66 features x 2 states) and was
	# enough on its own to push the native sweep past a 15-minute timeout. The
	# fixed seed and fixed step are what make the A/B reproducible; preprocess
	# only needs to be long enough for particles to be on screen at all.
	particles.preprocess = 0.4
	particles.collision_base_size = 0.14
	particles.transform = Transform3D(Basis(), Vector3(0, 0.4, 2.0))
	particles.visible = false
	particles.emitting = false
	add_child(particles)

	particle_collider = GPUParticlesCollisionSphere3D.new()
	particle_collider.radius = 1.6
	particle_collider.transform = Transform3D(Basis(), Vector3(0, 2.4, 2.0))
	particle_collider.visible = false
	add_child(particle_collider)

	particle_attractor = GPUParticlesAttractorSphere3D.new()
	particle_attractor.radius = 3.0
	particle_attractor.strength = -6.0
	particle_attractor.transform = Transform3D(Basis(), Vector3(2.0, 2.4, 2.0))
	particle_attractor.visible = false
	add_child(particle_attractor)


func _build_skinned() -> void:
	# A two-bone strip, bent by posing bone 1. Tests the GPU skinning path:
	# without it the mesh renders in rest pose and the A/B frame is identical.
	skeleton = Skeleton3D.new()
	# A skinned MeshInstance3D is rendered in its skeleton's space and ignores
	# its own transform, so this is what actually places the strip on screen.
	skeleton.transform = Transform3D(Basis(), Vector3(-4.6, 0.0, 1.2))
	add_child(skeleton)
	skeleton.add_bone("root")
	skeleton.set_bone_rest(0, Transform3D())
	skeleton.add_bone("tip")
	skeleton.set_bone_parent(1, 0)
	skeleton.set_bone_rest(1, Transform3D(Basis(), Vector3(0, 1.2, 0)))
	skeleton.reset_bone_poses()

	var segs := 8
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in segs:
		var y0 := float(i) / float(segs) * 2.4
		var y1 := float(i + 1) / float(segs) * 2.4
		# Clockwise: Godot front-faces are clockwise, and a counter-clockwise
		# strip here is back-face culled without any error at all.
		for s in [[-0.3, y0], [0.3, y1], [0.3, y0], [-0.3, y0], [-0.3, y1], [0.3, y1]]:
			var y: float = s[1]
			var w: float = clampf(y / 2.4, 0.0, 1.0)
			st.set_bones([0, 1, 0, 0])
			st.set_weights([1.0 - w, w, 0.0, 0.0])
			st.set_normal(Vector3(0, 0, 1))
			st.add_vertex(Vector3(s[0], y, 0))
	st.index()
	var mesh := st.commit()
	skinned = MeshInstance3D.new()
	skinned.mesh = mesh
	skinned.material_override = mat_floor
	skinned.visible = false
	add_child(skinned)
	skinned.skeleton = skinned.get_path_to(skeleton)
	var sk := Skin.new()
	sk.add_bind(0, Transform3D())
	sk.add_bind(1, Transform3D(Basis(), Vector3(0, -1.2, 0)))
	skinned.skin = sk


func _build_blendshape() -> void:
	# One blend shape that displaces every vertex sideways, so driving it from 0
	# to 1 is unmissable if blend shapes run and invisible if they do not.
	var base := PackedVector3Array()
	var norms := PackedVector3Array()
	for i in 6:
		# Clockwise, for the same reason as the skinned strip above.
		var quad := [Vector3(-0.6, 0, 0), Vector3(0.6, 1.8, 0), Vector3(0.6, 0, 0),
			Vector3(-0.6, 0, 0), Vector3(-0.6, 1.8, 0), Vector3(0.6, 1.8, 0)]
		base.append(quad[i])
		norms.append(Vector3(0, 0, 1))
	var shaped := PackedVector3Array()
	for v in base:
		shaped.append(v + Vector3(1.1, 0, 0))

	var mesh := ArrayMesh.new()
	mesh.add_blend_shape("push")
	mesh.blend_shape_mode = Mesh.BLEND_SHAPE_MODE_NORMALIZED
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = base
	arrays[Mesh.ARRAY_NORMAL] = norms
	var blend := []
	blend.resize(Mesh.ARRAY_MAX)
	blend[Mesh.ARRAY_VERTEX] = shaped
	blend[Mesh.ARRAY_NORMAL] = norms
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [blend])

	blendshape_inst = MeshInstance3D.new()
	blendshape_inst.mesh = mesh
	blendshape_inst.material_override = mat_emissive
	blendshape_inst.transform = Transform3D(Basis(), Vector3(4.2, 0.0, -1.0))
	blendshape_inst.visible = false
	add_child(blendshape_inst)


# ──────────────────────────────────────────────────────────────────────────────
# Driver
# ──────────────────────────────────────────────────────────────────────────────

func _process(_delta: float) -> void:
	if OS.has_feature("web"):
		var h: String = str(JavaScriptBridge.eval("location.hash", true))
		if h != _last_hash:
			_last_hash = h
			var idx := h.find("fp=")
			if idx >= 0:
				_request(h.substr(idx + 3))
	if _settle > 0:
		_settle -= 1
		if _settle == 0:
			print("[FP] READY %s %d" % [_pending_id, 1 if _pending_on else 0])
			_settle = -1


func _request(spec: String) -> void:
	var parts := spec.split(":")
	var id := parts[0].strip_edges()
	if id == "all":
		_exercise_all()
		return
	var on := parts.size() > 1 and parts[1].strip_edges().begins_with("1")
	_reset()
	if not _apply(id, on):
		print("[FP] UNKNOWN %s" % id)
		return
	_pending_id = id
	_pending_on = on
	_settle = int(SETTLE_OVERRIDE.get(id, SETTLE_DEFAULT))


## Return every mutable knob to the baseline, so each measurement is independent
## of whatever ran before it. Without this the matrix order would change results.
func _reset() -> void:
	env.background_mode = Environment.BG_SKY
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.ambient_light_energy = 1.0
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_exposure = 1.0
	env.ssao_enabled = false
	env.ssil_enabled = false
	env.ssr_enabled = false
	env.sdfgi_enabled = false
	env.glow_enabled = false
	env.glow_map = null
	env.glow_map_strength = 0.8
	env.fog_enabled = false
	env.fog_mode = Environment.FOG_MODE_EXPONENTIAL
	env.fog_aerial_perspective = 0.0
	env.fog_height_density = 0.0
	env.volumetric_fog_enabled = false
	env.adjustment_enabled = false
	env.adjustment_color_correction = null

	dir_light.visible = true
	dir_light.shadow_enabled = true
	dir_light.light_projector = null
	dir_light.light_angular_distance = 0.0
	dir_light.shadow_blur = 1.0
	dir_light.shadow_contact_shadows_allow = false
	dir_light.light_negative = false
	dir_light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	omni_light.visible = false
	omni_light.shadow_enabled = false
	omni_light.light_projector = null
	spot_light.visible = false
	spot_light.shadow_enabled = false
	spot_light.light_projector = null

	probe.visible = false
	voxel_gi.visible = false
	decal.visible = false
	multimesh_inst.visible = false
	skinned.visible = false
	blendshape_inst.visible = false
	particles.visible = false
	particles.emitting = false
	particles.trail_enabled = false
	particle_collider.visible = false
	particle_attractor.visible = false
	get_node("Glass").visible = false

	skeleton.set_bone_pose_rotation(1, Quaternion())
	if blendshape_inst.get_blend_shape_count() > 0:
		blendshape_inst.set_blend_shape_value(0, 0.0)
	lod_inst.visible = true
	lod_inst.lod_bias = 1.0
	fade_inst.visible = true
	fade_inst.visibility_range_end = 0.0

	mat_probe.normal_enabled = false
	mat_probe.rim_enabled = false
	mat_probe.clearcoat_enabled = false
	mat_probe.anisotropy_enabled = false
	mat_probe.subsurf_scatter_enabled = false
	mat_probe.backlight_enabled = false
	mat_probe.refraction_enabled = false
	mat_probe.heightmap_enabled = false
	mat_probe.detail_enabled = false
	mat_probe.emission_enabled = false
	mat_probe.proximity_fade_enabled = false
	mat_probe.distance_fade_mode = BaseMaterial3D.DISTANCE_FADE_DISABLED
	mat_probe.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
	mat_probe.uv1_triplanar = false
	mat_probe.metallic = 0.0
	mat_probe.roughness = 0.45
	mat_probe.albedo_color = Color(0.8, 0.75, 0.7)

	cam_attrs.dof_blur_far_enabled = false
	cam_attrs.dof_blur_near_enabled = false
	cam_attrs.auto_exposure_enabled = false

	var vp := get_viewport()
	vp.msaa_3d = Viewport.MSAA_DISABLED
	vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_DISABLED
	vp.use_taa = false
	vp.use_debanding = false
	vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
	vp.scaling_3d_scale = 1.0
	vp.mesh_lod_threshold = 1.0


## Apply one feature. Returns false for an unrecognized id so the harness can
## tell "not implemented here" apart from "made no difference".
func _apply(id: String, on: bool) -> bool:
	var vp := get_viewport()
	match id:
		# ---- environment ----
		"ambient_light":
			env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR if on else Environment.AMBIENT_SOURCE_DISABLED
			env.ambient_light_color = Color(0.6, 0.5, 0.9)
			env.ambient_light_energy = 2.0
		"reflected_light":
			env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY if on else Environment.REFLECTION_SOURCE_DISABLED
			mat_probe.metallic = 1.0
			mat_probe.roughness = 0.05
		"sky":
			env.background_mode = Environment.BG_SKY if on else Environment.BG_COLOR
			env.background_color = Color(0.05, 0.05, 0.06)
		"tonemap":
			env.tonemap_mode = Environment.TONE_MAPPER_ACES if on else Environment.TONE_MAPPER_LINEAR
		"tonemap_exposure":
			env.tonemap_exposure = 2.2 if on else 1.0
		"adjustments":
			env.adjustment_enabled = on
			env.adjustment_brightness = 1.5
			env.adjustment_contrast = 1.4
			env.adjustment_saturation = 2.0
		"color_correction":
			env.adjustment_enabled = on
			env.adjustment_color_correction = tex_lut if on else null
		"fog":
			env.fog_enabled = on
			env.fog_density = 0.08
			env.fog_light_color = Color(0.6, 0.65, 0.8)
		"fog_height":
			env.fog_enabled = on
			env.fog_density = 0.0
			env.fog_height = 0.5
			env.fog_height_density = 2.0
		"fog_aerial_perspective":
			env.fog_enabled = on
			env.fog_density = 0.02
			env.fog_aerial_perspective = 1.0
		"volumetric_fog":
			env.volumetric_fog_enabled = on
			env.volumetric_fog_density = 0.12
			spot_light.visible = true
			spot_light.shadow_enabled = true
			spot_light.light_volumetric_fog_energy = 8.0
		"glow":
			env.glow_enabled = on
			env.glow_intensity = 1.6
			env.glow_bloom = 0.5
			env.glow_hdr_threshold = 0.6
		"glow_map":
			env.glow_enabled = true
			env.glow_intensity = 1.6
			env.glow_hdr_threshold = 0.6
			env.glow_map = tex_gradient if on else null
			env.glow_map_strength = 1.0 if on else 0.0
		"ssr":
			env.ssr_enabled = on
			env.ssr_max_steps = 64
			mat_probe.metallic = 1.0
			mat_probe.roughness = 0.05
		"ssao":
			env.ssao_enabled = on
			env.ssao_intensity = 6.0
			env.ssao_radius = 2.0
		"ssil":
			env.ssil_enabled = on
			env.ssil_intensity = 4.0
			env.ssil_radius = 4.0
		"sdfgi":
			env.sdfgi_enabled = on
			env.sdfgi_energy = 4.0
			env.sdfgi_cascades = 2

		# ---- lights and shadows ----
		"directional_light":
			dir_light.visible = on
		"directional_shadow":
			dir_light.shadow_enabled = on
		"directional_shadow_splits":
			# Needs depth to be visible at all: one orthogonal shadow and a
			# 4-split cascade only diverge over a long receding range.
			multimesh_inst.visible = true
			dir_light.directional_shadow_mode = (DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS if on
				else DirectionalLight3D.SHADOW_ORTHOGONAL)
			dir_light.directional_shadow_max_distance = 120.0
		"omni_light":
			omni_light.visible = on
		"omni_shadow":
			omni_light.visible = true
			omni_light.shadow_enabled = on
		"spot_light":
			spot_light.visible = on
		"spot_shadow":
			spot_light.visible = true
			spot_light.shadow_enabled = on
		"light_projector":
			spot_light.visible = true
			spot_light.light_projector = tex_gradient if on else null
		"soft_shadows":
			# PCSS: a large light angular size turns a hard edge into a gradient.
			dir_light.light_angular_distance = 8.0 if on else 0.0
		"shadow_blur":
			dir_light.shadow_blur = 6.0 if on else 0.0
		"contact_shadows":
			dir_light.shadow_contact_shadows_allow = on
			dir_light.shadow_contact_shadows_opacity = 1.0
			dir_light.shadow_contact_shadows_blur = 0.0
		"light_negative":
			omni_light.visible = true
			omni_light.light_negative = on
		"light_specular":
			mat_probe.metallic = 1.0
			mat_probe.roughness = 0.08
			dir_light.light_specular = 1.0 if on else 0.0

		# ---- GI ----
		"reflection_probe":
			probe.visible = on
			mat_probe.metallic = 1.0
			mat_probe.roughness = 0.1
		"voxel_gi":
			if on and voxel_gi.data == null:
				voxel_gi.bake(self, false)
			voxel_gi.visible = on
		"decal":
			decal.visible = on

		# ---- materials ----
		"normal_map":
			mat_probe.normal_enabled = on
			mat_probe.normal_texture = tex_normal
			mat_probe.normal_scale = 4.0
		"emission":
			mat_probe.emission_enabled = on
			mat_probe.emission = Color(0.9, 0.3, 0.1)
			mat_probe.emission_energy_multiplier = 4.0
		"rim":
			mat_probe.rim_enabled = on
			mat_probe.rim = 1.0
			mat_probe.rim_tint = 0.0
		"clearcoat":
			mat_probe.clearcoat_enabled = on
			mat_probe.clearcoat = 1.0
			mat_probe.clearcoat_roughness = 0.0
		"anisotropy":
			mat_probe.anisotropy_enabled = on
			mat_probe.anisotropy = 0.95
			mat_probe.metallic = 1.0
			mat_probe.roughness = 0.35
		"subsurface_scattering":
			mat_probe.subsurf_scatter_enabled = on
			mat_probe.subsurf_scatter_strength = 1.0
		"backlight":
			mat_probe.backlight_enabled = on
			mat_probe.backlight = Color(0.9, 0.2, 0.1)
		"refraction":
			mat_probe.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			mat_probe.albedo_color = Color(0.9, 0.95, 1.0, 0.5)
			mat_probe.refraction_enabled = on
			mat_probe.refraction_scale = 0.4
		"heightmap":
			mat_probe.heightmap_enabled = on
			mat_probe.heightmap_texture = tex_normal
			mat_probe.heightmap_scale = 16.0
		"triplanar":
			mat_probe.uv1_triplanar = on
			mat_probe.uv1_scale = Vector3(0.35, 0.35, 0.35)
		"transparency":
			get_node("Glass").visible = on
		"alpha_scissor":
			mat_probe.transparency = (BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR if on
				else BaseMaterial3D.TRANSPARENCY_DISABLED)
			mat_probe.albedo_texture = tex_gradient
			mat_probe.alpha_scissor_threshold = 0.5
		"alpha_hash":
			mat_probe.transparency = (BaseMaterial3D.TRANSPARENCY_ALPHA_HASH if on
				else BaseMaterial3D.TRANSPARENCY_DISABLED)
			mat_probe.albedo_texture = tex_gradient
			mat_probe.alpha_hash_scale = 1.0
		"proximity_fade":
			mat_probe.proximity_fade_enabled = on
			mat_probe.proximity_fade_distance = 4.0
		"distance_fade":
			mat_probe.distance_fade_mode = (BaseMaterial3D.DISTANCE_FADE_PIXEL_ALPHA if on
				else BaseMaterial3D.DISTANCE_FADE_DISABLED)
			mat_probe.distance_fade_min_distance = 12.0
			mat_probe.distance_fade_max_distance = 4.0

		# ---- mesh pipeline ----
		"multimesh":
			multimesh_inst.visible = on
		"skeletal_animation":
			skinned.visible = true
			skeleton.set_bone_pose_rotation(1, Quaternion(Vector3(0, 0, 1), 2.2) if on else Quaternion())
		"blend_shapes":
			blendshape_inst.visible = true
			blendshape_inst.set_blend_shape_value(0, 1.0 if on else 0.0)
		"mesh_lod":
			vp.mesh_lod_threshold = 64.0 if on else 0.0
		"visibility_range":
			fade_inst.visibility_range_end = 2.0 if on else 0.0

		# ---- particles ----
		"gpu_particles":
			particles.visible = on
			particles.emitting = on
		"particle_collision":
			particles.visible = true
			particles.emitting = true
			particle_collider.visible = on
		"particle_attractor":
			particles.visible = true
			particles.emitting = true
			particle_attractor.visible = on
		"particle_trails":
			particles.visible = true
			particles.emitting = true
			particles.trail_enabled = on
			particles.trail_lifetime = 0.6

		# ---- post / AA / scaling ----
		"msaa":
			vp.msaa_3d = Viewport.MSAA_4X if on else Viewport.MSAA_DISABLED
		"taa":
			vp.use_taa = on
		"fxaa":
			vp.screen_space_aa = (Viewport.SCREEN_SPACE_AA_FXAA if on
				else Viewport.SCREEN_SPACE_AA_DISABLED)
		"debanding":
			vp.use_debanding = on
		"scaling_3d":
			vp.scaling_3d_scale = 0.5 if on else 1.0
		"fsr2":
			vp.scaling_3d_mode = (Viewport.SCALING_3D_MODE_FSR2 if on
				else Viewport.SCALING_3D_MODE_BILINEAR)
			vp.scaling_3d_scale = 0.5
		"dof_far":
			cam_attrs.dof_blur_far_enabled = on
			cam_attrs.dof_blur_far_distance = 6.0
			cam_attrs.dof_blur_far_transition = 1.0
			cam_attrs.dof_blur_amount = 0.3
		"dof_near":
			cam_attrs.dof_blur_near_enabled = on
			cam_attrs.dof_blur_near_distance = 6.0
			cam_attrs.dof_blur_near_transition = 1.0
			cam_attrs.dof_blur_amount = 0.3
		"auto_exposure":
			cam_attrs.auto_exposure_enabled = on
			cam_attrs.auto_exposure_scale = 0.2
		_:
			return false
	return true
