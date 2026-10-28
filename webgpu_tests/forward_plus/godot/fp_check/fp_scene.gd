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
##   location.hash = "#fp=<feature_id>:<0|1>[:<settle frames>]"
## The script polls the hash, applies the config, waits the feature's settle
## frames (or the given count, which the parity mode uses), then prints:
##   [FP] READY <feature_id> <0|1>
## and freezes that frame -- particles stop and the render loop pauses after
## drawing it -- so the harness's asynchronous screenshot captures exactly the
## frame READY announced, not one some frames later. Without that, everything
## animated or temporal (particles, TAA and FSR2 jitter) differed from the
## native reference by however late the capture landed.
## and on an unknown id:
##   [FP] UNKNOWN <feature_id>
## so a feature added to features.mjs without a matching arm here fails loudly
## rather than silently comparing a frame against itself.
##
## Everything is built procedurally. That is deliberate: a .tscn with baked
## resources cannot express a skinned mesh, blend shapes or a MultiMesh without
## binary assets, and binary fixtures are the kind of thing that rots silently
## across upstream syncs. The exceptions are editor products a shipped project
## loads rather than builds: lod_sphere.res, voxel_gi_data.res and the baked
## lightmap_room (bake_lightmap.sh).

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
	"volumetric_fog", "glow", "glow_map", "ssr", "ssao", "ssil", "sdfgi", "sdfgi_camera_move",
	"directional_light", "directional_shadow", "directional_shadow_splits",
	"omni_light", "omni_shadow", "omni_shadow_cube", "spot_light", "spot_shadow", "light_projector",
	"soft_shadows", "shadow_blur", "contact_shadows", "light_negative", "light_specular",
	"reflection_probe", "voxel_gi", "decal",
	"normal_map", "emission", "rim", "clearcoat", "anisotropy",
	"subsurface_scattering", "backlight", "refraction", "heightmap", "triplanar",
	"transparency", "alpha_scissor", "alpha_hash", "proximity_fade", "distance_fade",
	"depth_texture", "mipmap_bias", "texture_array", "shader_builtins", "point_coord",
	"stencil_outline", "stencil_xray", "bent_normal",
	"fog_volume", "fog_volume_shader", "lightmap_gi",
	"multimesh", "skeletal_animation", "blend_shapes", "mesh_lod", "visibility_range", "occlusion_culling",
	"gpu_particles", "particle_collision", "particle_attractor", "particle_trails",
	"msaa", "taa", "fxaa", "smaa", "debanding", "scaling_3d", "fsr2",
	"dof_far", "dof_near", "dof_nan", "auto_exposure", "white_balance", "compositor_effect",
	"glow_nan", "ssr_nan", "auto_exposure_nan",
	# CameraAttributes / Environment camera effects (engine additions, TASKS.md 4.52-4.55); only the deterministic
	# ones: grain, zebras and motion-dependent effects change from frame to frame.
	"lens_optics", "bokeh_shape", "longitudinal_ca", "tilt_shift", "starburst", "halation",
	"lens_flare", "veiling_glare_diffusion", "camera_lut", "in_camera_processing",
	# Environment micro shadows (directional light shadowed by the material's AO and normal maps, scene shaders).
	"micro_shadows",
	# GTAO (engine addition, project setting rendering/environment/ssao/method).
	"gtao",
	# CameraAttributes local exposure (engine addition, work task 57): the bilateral grid and its slicing.
	"local_exposure",
]

const SETTLE_DEFAULT := 4
# Temporal effects pick their jitter from the frame number: volumetric fog's
# Halton offset is frame % 16 (VolumetricFog::MAX_TEMPORAL_FRAMES), TAA's jitter
# is frame % 16, and FSR2's is frame % (8 / scale^2), 32 at the matrix's 0.5
# scale (RendererViewport, from ffxFsr2GetJitterPhaseCount). Two captures of a
# settled temporal effect are only comparable at the same phase of that cycle,
# so every capture is made to land on a frame whose number is _phase modulo this
# period (_aligned()), a multiple of all three. With 16, FSR2 captures landed on
# phase 0 or 16 of its cycle at random on the web (0.14 vs 0.17 from native).
const JITTER_PERIOD := 32
# Features whose result accumulates over frames. Undershooting these produces a
# flaky "feature does nothing" failure, which is worse than a slow test.
const SETTLE_OVERRIDE := {
	"sdfgi": 45,
	"volumetric_fog": 30,
	"fog_volume": 30,
	"fog_volume_shader": 30,
	"lightmap_gi": 12,
	# The occlusion buffer is rasterized from the previous frame's camera.
	"occlusion_culling": 8,
	"taa": 24,
	"auto_exposure": 40,
	"auto_exposure_nan": 40,
	"ssr": 10,
	"ssr_nan": 10,
	"ssil": 10,
	"gpu_particles": 40,
	"particle_trails": 50,
	"particle_collision": 50,
	"particle_attractor": 50,
	"reflection_probe": 12,
	"voxel_gi": 20,
	"fsr2": 16,
	# The camera flies 60 m away (scrolling every SDFGI cascade) and back over
	# 240 frames, then SDFGI settles where it started.
	"sdfgi_camera_move": 330,
}

var env: Environment
var cam: Camera3D
var cam_attrs: CameraAttributesPractical
# The physical camera of the arms that need a lens (tilt_shift, longitudinal_ca); cam wears cam_attrs otherwise.
var cam_attrs_physical: CameraAttributesPhysical
# A small, very bright sphere in the sky behind the far focus, for the camera's glare and bokeh arms.
const GLARE_SPHERE_XFORM := Transform3D(Basis(), Vector3(2.5, 3.0, -6.0))
var glare_sphere: MeshInstance3D
var tex_lut3d: ImageTexture3D
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
var trail_particles: GPUParticles3D
var fog_box: FogVolume
var fog_ellipsoid: FogVolume
var fog_shaded: FogVolume
var lightmap_room: Node3D
var lightmap_gi: LightmapGI
var occ_wall: MeshInstance3D
var occluder: OccluderInstance3D
var occluded := []                     # the MeshInstance3Ds hidden behind occ_wall
var compositor_effect: CompositorEffect
var blendshape_inst: MeshInstance3D
var lod_inst: MeshInstance3D
var fade_inst: MeshInstance3D
# Where the NaN quad sits: in the sky, deep in dof_nan's far blur, about 2x2 pixels.
const NAN_QUAD_XFORM := Transform3D(Basis(), Vector3(3.5, 4.5, -12.0))
var nan_quad: MeshInstance3D             # writes NaN into the scene color (dof_nan and the *_nan arms)

var mat_floor: StandardMaterial3D
var mat_probe: StandardMaterial3D      # the material feature tests mutate this one
var mat_glass: StandardMaterial3D
var mat_emissive: StandardMaterial3D

var tex_checker: ImageTexture
var tex_checker_mips: ImageTexture
var tex_normal: ImageTexture
var tex_gradient: ImageTexture
var tex_lut: ImageTexture
var tex_bent: ImageTexture
var tex_ao: ImageTexture

var probe_mi: MeshInstance3D           # wears mat_probe unless a test swaps in a ShaderMaterial
var mat_depth: ShaderMaterial
var mat_array: ShaderMaterial
var mat_builtins: ShaderMaterial
var mat_points: ShaderMaterial

# Every stored property of the fixture's resources as built, restored by
# _reset() so no test can leak a setting into the next one.
var _resource_defaults := {}

# sdfgi_camera_move's flight: frame counter (-1 when not flying) and start.
var _move_frame := -1
var _cam_base: Transform3D

# Settle frames to use instead of each feature's own (--fp-settle=N natively,
# the hash's third field on the web), or -1.
var _settle_override := -1

var _last_hash := ""
# A request read from the hash, waiting for its start frame (see _aligned()).
var _queued_spec := ""
# The jitter phase every captured frame lands on (--fp-phase=N natively, for triage).
var _phase := 0
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
	_build_shader_materials()
	_build_camera_effects()
	_snapshot_resources()
	# Native runs (no browser) take the feature from the command line so the
	# same fixture can be driven against Vulkan for a cross-backend reference.
	print("[FP] FEATURES %s" % ",".join(FEATURES))
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--fp-settle="):
			_settle_override = int(a.substr(12))
		if a.begins_with("--fp-phase="):
			_phase = posmod(int(a.substr(11)), JITTER_PERIOD)
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
##
## The parity mode adds a fixed timestep and one long settle for every feature,
## so both sides have simulated exactly the same time and every accumulating
## effect has converged:
##   godot --path . --rendering-driver vulkan --resolution 640x360 --fixed-fps 60 \
##         -- --fp-matrix=/tmp/fpref --fp-settle=240
##
## --fp-only=a,b limits the sweep to those features, for triage.
func _run_native_matrix(dir: String) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	var only := PackedStringArray()
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--fp-only="):
			only = a.substr(10).split(",", false)
	for id in FEATURES:
		if not only.is_empty() and not only.has(id):
			continue
		for on in [false, true]:
			# Same timeline as the web side: the config is applied during the
			# process step of the frame _aligned() picks, and the frame captured
			# is the one drawn settle - 1 frames later.
			var settle := _settle_for(id)
			await get_tree().process_frame
			while not _aligned(settle):
				await get_tree().process_frame
			_reset()
			if not _apply(id, on):
				print("[FP] UNKNOWN %s" % id)
				continue
			for i in settle - 1:
				await get_tree().process_frame
			# Freeze at the same point the web side does (see _process()).
			_freeze_particles()
			await RenderingServer.frame_post_draw
			print("[FP] FRAME %d (phase %d)" % [Engine.get_frames_drawn(), posmod(Engine.get_frames_drawn(), JITTER_PERIOD)])
			var img := get_viewport().get_texture().get_image()
			img.save_png("%s/%s_%d.png" % [dir, id, 1 if on else 0])
			# What the frame drew, for arms whose correct result is pixel-identical
			# (occlusion_culling); run_forward_plus.mjs compares it with the web's.
			var f := FileAccess.open("%s/%s_%d.info" % [dir, id, 1 if on else 0], FileAccess.WRITE)
			f.store_line(_render_info())
			f.close()
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
	# The same checker with mipmaps, for mipmap_bias: a texture without them has
	# nothing for a LOD bias to select.
	var mip_img := img.duplicate()
	mip_img.generate_mipmaps()
	tex_checker_mips = ImageTexture.create_from_image(mip_img)

	# Bumpy normal map (tangent space).
	var nimg := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var nx := sin(float(x) * 0.6) * 0.5
			var ny := cos(float(y) * 0.6) * 0.5
			nimg.set_pixel(x, y, Color(nx * 0.5 + 0.5, ny * 0.5 + 0.5, 1.0))
	tex_normal = ImageTexture.create_from_image(nimg)

	# Ambient occlusion matching the bumps: dark in the troughs of the normal map's waves, for micro_shadows.
	var aoimg := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var a := 0.2 + 0.8 * (0.5 + 0.25 * (cos(float(x) * 0.6) + sin(float(y) * 0.6)))
			aoimg.set_pixel(x, y, Color(a, a, a))
	tex_ao = ImageTexture.create_from_image(aoimg)

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

	# Bent normal map: bands bent hard up and down, so the ambient light a pixel
	# gathers comes from a visibly different part of the sky (blue top, brown
	# ground).
	var bimg := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	for y in 64:
		for x in 64:
			var by := sin(float(y) * 0.4) * 0.9
			var bz := sqrt(maxf(1.0 - by * by, 0.0))
			bimg.set_pixel(x, y, Color(0.5, by * 0.5 + 0.5, bz * 0.5 + 0.5))
	tex_bent = ImageTexture.create_from_image(bimg)


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
	probe_mi = _mesh_instance(sph, mat_probe, Transform3D(Basis(), Vector3(0, 1.2, 0)))

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
	# The LOD subject is a committed mesh with its LOD chain already baked
	# (make_lod_mesh.gd regenerates it). Generating LODs at runtime instead
	# looks tempting but is wrong twice over: ImporterMesh.generate_lods()
	# produces zero LODs in a web template (it works in a native one, so this
	# is web-specific), and runtime generation is not how a shipped project
	# gets LODs anyway -- the importer bakes them. A committed resource tests
	# the path users actually ship.
	var lod_res: Mesh = load("res://lod_sphere.res")
	# Reported because a missing resource and broken LOD switching produce the
	# same zero delta, and the two need telling apart.
	print("[FP] LODSUBJECT %s" % ("baked" if lod_res else "MISSING -- mesh_lod cannot pass"))
	lod_inst = _mesh_instance(lod_res if lod_res else lod_mesh, mat_box,
		Transform3D(Basis(), Vector3(-4.6, 0.8, 2.0)))
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

	# VoxelGI needs baked data, loaded below from a committed resource.
	voxel_gi = VoxelGI.new()
	voxel_gi.size = Vector3(16, 8, 16)
	voxel_gi.transform = Transform3D(Basis(), Vector3(0, 3.0, 0))
	var vg_data: VoxelGIData = load("res://voxel_gi_data.res")
	if vg_data:
		voxel_gi.data = vg_data
	print("[FP] VOXELGI %s" % ("baked" if vg_data else "MISSING"))
	voxel_gi.visible = false
	add_child(voxel_gi)


func _build_extras() -> void:
	# dof_nan: a quad of about 2x2 pixels in the far blur that writes NaN. The NaN comes from a
	# uniform's bits at run time: no compiler can fold it, and WGSL forbids a NaN constant expression.
	var nan_mat := ShaderMaterial.new()
	nan_mat.shader = Shader.new()
	nan_mat.shader.code = """shader_type spatial;
render_mode unshaded, cull_disabled;
uniform uint nan_bits = 2143289344u;
void fragment() {
	ALBEDO = vec3(uintBitsToFloat(nan_bits));
}
"""
	var nan_mesh := QuadMesh.new()
	nan_mesh.size = Vector2(0.12, 0.12)
	nan_quad = _mesh_instance(nan_mesh, nan_mat, NAN_QUAD_XFORM)
	nan_quad.visible = false

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
	_build_trails()
	_build_fog_volumes()
	_build_occlusion()
	_build_lightmap()
	_build_compositor()

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
	# A two-bone strip, bent by posing bone 1. Tests the GPU skinning path
	# (the skeleton compute pass): without it the strip renders in rest pose and
	# the A/B frame is identical. Laid out the way an imported skinned model is
	# -- the MeshInstance3D a child of its Skeleton3D -- so the skeleton's
	# transform and the instance's agree and nothing depends on which one the
	# renderer uses. The old strip (a sibling of the skeleton, behind the LOD
	# sphere, in the floor's gray) moved 0.045 on native for a 2.2 rad bend.
	skeleton = Skeleton3D.new()
	skeleton.transform = Transform3D(Basis(), Vector3(1.6, 0.0, 2.4))
	add_child(skeleton)
	skeleton.add_bone("root")
	skeleton.set_bone_rest(0, Transform3D())
	skeleton.add_bone("tip")
	skeleton.set_bone_parent(1, 0)
	skeleton.set_bone_rest(1, Transform3D(Basis(), Vector3(0, 1.2, 0)))
	skeleton.reset_bone_poses()

	var segs := 12
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in segs:
		var y0 := float(i) / float(segs) * 2.4
		var y1 := float(i + 1) / float(segs) * 2.4
		for s in [[-0.4, y0], [0.4, y1], [0.4, y0], [-0.4, y0], [-0.4, y1], [0.4, y1]]:
			var y: float = s[1]
			# Rigid below the joint, blending across it, rigid above: a hinge.
			var w: float = clampf((y - 0.9) / 0.6, 0.0, 1.0)
			st.set_bones(PackedInt32Array([0, 1, 0, 0]))
			st.set_weights(PackedFloat32Array([1.0 - w, w, 0.0, 0.0]))
			st.set_normal(Vector3(0, 0, 1))
			st.add_vertex(Vector3(s[0], y, 0))
	var mesh := st.commit()
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.95, 0.45, 0.15)
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	skinned = MeshInstance3D.new()
	skinned.mesh = mesh
	skinned.material_override = mat
	skinned.visible = false
	skeleton.add_child(skinned)
	skinned.skeleton = NodePath("..")
	var sk := Skin.new()
	sk.add_bind(0, Transform3D())
	sk.add_bind(1, Transform3D(Basis(), Vector3(0, -1.2, 0)))
	skinned.skin = sk


func _build_trails() -> void:
	# Particle trails: each particle drags a RibbonTrailMesh skinned along its
	# recent path (the trail bones the particles shader writes), aligned with
	# TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY as ribbons need. Its own node,
	# because trails need a trail draw pass and use_particle_trails on the
	# material, which the matrix's sphere particles do not have. Off is the same
	# ribbons with trail_enabled false, drawn rigid.
	trail_particles = GPUParticles3D.new()
	var pmat := ParticleProcessMaterial.new()
	pmat.direction = Vector3(1, 1.4, 0)
	pmat.spread = 30.0
	pmat.initial_velocity_min = 3.0
	pmat.initial_velocity_max = 4.5
	pmat.gravity = Vector3(0, -5.0, 0)
	trail_particles.process_material = pmat
	var ribbon := RibbonTrailMesh.new()
	ribbon.size = 0.18
	ribbon.sections = 8
	ribbon.section_length = 0.1
	var tmat := StandardMaterial3D.new()
	tmat.use_particle_trails = true
	tmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	tmat.cull_mode = BaseMaterial3D.CULL_DISABLED
	tmat.albedo_color = Color(0.3, 0.9, 1.0)
	ribbon.material = tmat
	trail_particles.draw_pass_1 = ribbon
	trail_particles.transform_align = GPUParticles3D.TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY
	trail_particles.amount = 24
	trail_particles.lifetime = 1.2
	trail_particles.trail_lifetime = 0.5
	trail_particles.use_fixed_seed = true
	trail_particles.seed = 20261026
	trail_particles.fixed_fps = 30
	trail_particles.interpolate = false
	trail_particles.preprocess = 0.4
	trail_particles.transform = Transform3D(Basis(), Vector3(-2.6, 0.3, 2.2))
	trail_particles.visible = false
	trail_particles.emitting = false
	add_child(trail_particles)


func _build_fog_volumes() -> void:
	# FogVolume nodes add density to the volumetric fog froxels by shape, with a
	# built-in FogMaterial or a user `shader_type fog` shader. The arms run with
	# the environment's own fog density at 0, so all the fog is the volumes'.
	var fm := FogMaterial.new()
	fm.density = 1.2
	fm.albedo = Color(1.0, 0.7, 0.45)
	fm.emission = Color(0.15, 0.06, 0.02)
	fog_box = FogVolume.new()
	fog_box.shape = RenderingServer.FOG_VOLUME_SHAPE_BOX
	fog_box.size = Vector3(1.6, 2.6, 1.6)
	fog_box.material = fm
	fog_box.transform = Transform3D(Basis(), Vector3(-1.9, 1.3, 1.6))
	fog_box.visible = false
	add_child(fog_box)
	fog_ellipsoid = FogVolume.new()
	fog_ellipsoid.shape = RenderingServer.FOG_VOLUME_SHAPE_ELLIPSOID
	fog_ellipsoid.size = Vector3(2.2, 1.8, 2.2)
	fog_ellipsoid.material = fm
	fog_ellipsoid.transform = Transform3D(Basis(), Vector3(1.9, 1.4, 1.6))
	fog_ellipsoid.visible = false
	add_child(fog_ellipsoid)

	var fs := Shader.new()
	fs.code = """shader_type fog;
uniform vec3 tint = vec3(0.3, 0.6, 1.0);
void fog() {
	float bands = step(0.5, fract(WORLD_POSITION.x * 1.2 + WORLD_POSITION.y * 0.8));
	DENSITY = 1.5 * bands * clamp(1.0 - length(UVW - 0.5) * 1.4, 0.0, 1.0);
	ALBEDO = tint;
	EMISSION = tint * 0.25;
}
"""
	var fsm := ShaderMaterial.new()
	fsm.shader = fs
	fog_shaded = FogVolume.new()
	fog_shaded.shape = RenderingServer.FOG_VOLUME_SHAPE_BOX
	fog_shaded.size = Vector3(4.0, 2.4, 1.6)
	fog_shaded.material = fsm
	fog_shaded.transform = Transform3D(Basis(), Vector3(0.0, 1.2, 2.2))
	fog_shaded.visible = false
	add_child(fog_shaded)


func _build_occlusion() -> void:
	# An opaque wall with a matching occluder, and small boxes behind it. With
	# occlusion culling the boxes (and the probe sphere, also behind it) are
	# culled before they are drawn; the picture must not change at all. The
	# occluder is an ArrayOccluder3D built from the wall's own triangles, which
	# is what baking an OccluderInstance3D produces. The wall is also the
	# obstacle the stencil_xray arm sees through.
	var wall_mesh := BoxMesh.new()
	wall_mesh.size = Vector3(2.8, 2.8, 0.1)
	var wall_mat := StandardMaterial3D.new()
	wall_mat.albedo_color = Color(0.55, 0.5, 0.45)
	wall_mat.roughness = 0.8
	var xform := Transform3D(Basis(), Vector3(0, 1.4, 4.0))
	occ_wall = _mesh_instance(wall_mesh, wall_mat, xform)
	occ_wall.visible = false
	var arrays := wall_mesh.get_mesh_arrays()
	var occ := ArrayOccluder3D.new()
	occ.set_arrays(arrays[Mesh.ARRAY_VERTEX], arrays[Mesh.ARRAY_INDEX])
	occluder = OccluderInstance3D.new()
	occluder.occluder = occ
	occluder.transform = xform
	occluder.visible = false
	add_child(occluder)
	var bm := BoxMesh.new()
	bm.size = Vector3(0.2, 0.2, 0.2)
	for ix in 5:
		for iy in 4:
			var b := _mesh_instance(bm, wall_mat,
				Transform3D(Basis(), Vector3(-0.8 + ix * 0.4, 0.6 + iy * 0.45, 2.8)))
			# Their shadows would land in view, beside the wall.
			b.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			b.visible = false
			occluded.append(b)


func _build_lightmap() -> void:
	# A small room baked with LightmapGI (bake_lightmap.sh regenerates the bake;
	# the editor's own Bake Lightmaps action, scripted). Its light is static, so
	# with the lightmap its direct light and two bounces come from the baked
	# texture, and without it the same light is computed in real time. Parked
	# far from the main scene and hidden unless the arm is running.
	var room_scene: PackedScene = load("res://lightmap_room.tscn")
	lightmap_room = room_scene.instantiate() if room_scene else Node3D.new()
	lightmap_room.position = Vector3(-60, 0, 0)
	lightmap_room.visible = false
	add_child(lightmap_room)
	lightmap_gi = lightmap_room.get_node_or_null("LightmapGI")
	var data: LightmapGIData = lightmap_gi.light_data if lightmap_gi else null
	print("[FP] LIGHTMAP %s" % ("baked" if data and not data.lightmap_textures.is_empty() else "MISSING -- lightmap_gi cannot pass"))
	if lightmap_gi:
		lightmap_gi.set_meta("baked", data)
		lightmap_gi.light_data = null


func _build_compositor() -> void:
	compositor_effect = load("res://fp_compositor_effect.gd").new()
	var comp := Compositor.new()
	comp.compositor_effects = [compositor_effect]
	cam.compositor = comp


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


func _shader_material(code: String) -> ShaderMaterial:
	var sh := Shader.new()
	sh.code = code
	var m := ShaderMaterial.new()
	m.shader = sh
	return m


func _solid_texture(c: Color) -> ImageTexture:
	var img := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(c)
	return ImageTexture.create_from_image(img)


func _build_shader_materials() -> void:
	# Reads DEPTH_TEXTURE behind the probe and shows view-space depth as 2 m
	# bands. Transparent, so it draws after the depth copy it reads.
	mat_depth = _shader_material("""shader_type spatial;
render_mode unshaded, depth_draw_never;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;
void fragment() {
	float d = textureLod(depth_texture, SCREEN_UV, 0.0).r;
	vec4 v = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, d, 1.0);
	ALBEDO = vec3(fract(-v.z / v.w * 0.5));
	ALPHA = 0.999;
}
""")

	# An array of four textures read at a runtime index. off shows element 0,
	# on element 2: if the array collapsed to its first element, both frames
	# would be the same colour.
	mat_array = _shader_material("""shader_type spatial;
render_mode unshaded;
uniform sampler2D layers[4] : filter_nearest;
uniform int layer = 0;
void fragment() {
	ALBEDO = texture(layers[layer], UV).rgb;
}
""")
	mat_array.set_shader_parameter("layers", [
		_solid_texture(Color(0.9, 0.1, 0.1)), _solid_texture(Color(0.1, 0.9, 0.1)),
		_solid_texture(Color(0.1, 0.2, 0.9)), _solid_texture(Color(0.9, 0.8, 0.1))])

	# Built-ins Tint's SPIR-V reader used to abort on -- isnan(), isinf() and
	# textureQueryLod() -- which in a browser took the whole page down the
	# moment the material compiled. POINT_COORD, the fourth, has its own arm:
	# using it turns the mesh into points. Both states use this shader,
	# so a crash fails both; on tints the probe and folds in what the built-ins
	# return (compared against native in the parity mode). The isnan()/isinf()
	# inputs are finite: what a NaN or infinity tests as is up to the GPU
	# driver's float model (NVIDIA's Vulkan driver says false for both), and
	# this test is about the port, not the driver.
	mat_builtins = _shader_material("""shader_type spatial;
uniform sampler2D tex : filter_linear_mipmap;
uniform bool use_builtins = false;
uniform float finite = 1.0;
void fragment() {
	vec3 c = texture(tex, UV).rgb;
	if (use_builtins) {
		vec2 lod = textureQueryLod(tex, UV * 64.0);
		c += vec3(0.3, 0.15, 0.0) + vec3(isnan(finite) ? 0.3 : 0.0, isinf(finite) ? 0.2 : 0.0, clamp(lod.x, 0.0, 6.0) * 0.05);
	}
	ALBEDO = c;
}
""")
	mat_builtins.set_shader_parameter("tex", tex_checker_mips)

	# Reading POINT_COORD (without writing POINT_SIZE) draws the mesh as
	# points. Where point size is emulated (WebGPU, D3D12) that drew nothing
	# at all: the quad expansion only existed under POINT_SIZE_USED. Both
	# states draw points, so a material that renders nothing has no delta.
	mat_points = _shader_material("""shader_type spatial;
render_mode unshaded;
uniform bool use_coord = false;
void fragment() {
	ALBEDO = use_coord ? vec3(POINT_COORD, 0.0) : vec3(1.0);
}
""")


## The camera effects' scene content and resources: the glare sphere, a physical camera framed like cam_attrs's
## 55 degree view, and a 3D LUT (a warm grade with a lifted, cooled shadow).
func _build_camera_effects() -> void:
	var sphere := SphereMesh.new()
	sphere.radius = 0.12
	sphere.height = 0.24
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(1.0, 0.95, 0.85)
	mat.emission_enabled = true
	mat.emission = Color(1.0, 0.95, 0.85)
	mat.emission_energy_multiplier = 60.0
	glare_sphere = _mesh_instance(sphere, mat, GLARE_SPHERE_XFORM)
	glare_sphere.visible = false

	cam_attrs_physical = CameraAttributesPhysical.new()
	# 24 mm tall full-frame sensor: 12 / tan(27.5 deg) = 23.05 mm gives cam's 55 degree vertical view.
	cam_attrs_physical.frustum_focal_length = 23.05
	cam_attrs_physical.frustum_focus_distance = 4.0
	# f/1.4 for a deep blur; the project does not use physical light units, so the exposure stays cam_attrs's.
	cam_attrs_physical.exposure_aperture = 1.4

	const N := 17
	var images: Array[Image] = []
	for b in N:
		var img := Image.create_empty(N, N, false, Image.FORMAT_RGBA8)
		for g in N:
			for r in N:
				var c := Vector3(r, g, b) / float(N - 1)
				var o := Vector3(pow(c.x, 0.85), c.y, c.z * 0.85 + 0.06 * (1.0 - c.z))
				img.set_pixel(r, g, Color(o.x, o.y, o.z))
		images.append(img)
	tex_lut3d = ImageTexture3D.new()
	tex_lut3d.create(Image.FORMAT_RGBA8, N, N, N, false, images)


func _use_physical_camera() -> void:
	cam.attributes = cam_attrs_physical


## Record every stored property of the fixture's resources, so _reset() can put
## all of them back. Listing knobs by hand there missed some -- alpha_scissor's
## albedo texture stayed on the probe for every test after it, and fog_height
## inherited fog's colour -- so results depended on matrix order.
func _snapshot_resources() -> void:
	var particle_material: ParticleProcessMaterial = particles.process_material
	for r: Resource in [env, cam_attrs, cam_attrs_physical, mat_probe, mat_glass, particle_material]:
		var values := {}
		for prop in r.get_property_list():
			if (prop.usage & PROPERTY_USAGE_STORAGE) and prop.name != "script" and not prop.name.begins_with("resource_"):
				values[prop.name] = r.get(prop.name)
		_resource_defaults[r] = values


func _restore_resources() -> void:
	for r: Resource in _resource_defaults:
		var values: Dictionary = _resource_defaults[r]
		for name: String in values:
			if r.get(name) != values[name]:
				r.set(name, values[name])


# ──────────────────────────────────────────────────────────────────────────────
# Driver
# ──────────────────────────────────────────────────────────────────────────────

func _process(_delta: float) -> void:
	if _move_frame >= 0:
		# Out over 90 frames, hold 60, back over 90.
		_move_frame += 1
		var t := 0.0
		if _move_frame < 90:
			t = _move_frame / 90.0
		elif _move_frame < 150:
			t = 1.0
		elif _move_frame < 240:
			t = 1.0 - (_move_frame - 150) / 90.0
		else:
			_move_frame = -1
		cam.transform = _cam_base.translated(Vector3(60.0 * t, 0, 0))
	if OS.has_feature("web"):
		var h: String = str(JavaScriptBridge.eval("location.hash", true))
		if h != _last_hash:
			_last_hash = h
			var idx := h.find("fp=")
			if idx >= 0:
				_queued_spec = h.substr(idx + 3)
				# Frames must be drawn to reach the start frame; while frozen, none are.
				RenderingServer.render_loop_enabled = true
		if not _queued_spec.is_empty() and _spec_aligned(_queued_spec):
			var spec := _queued_spec
			_queued_spec = ""
			_request(spec)
	if _settle > 0:
		_settle -= 1
		if _settle == 0:
			# Freeze this frame for the harness (see the protocol note above).
			_freeze_particles()
			RenderingServer.frame_post_draw.connect(func(): RenderingServer.render_loop_enabled = false, CONNECT_ONE_SHOT)
			print("[FP] FRAME %d (phase %d)" % [Engine.get_frames_drawn(), posmod(Engine.get_frames_drawn(), JITTER_PERIOD)])
			# The previous frame drew this config too (every settle is >= 2).
			print("[FP] INFO %s" % _render_info())
			print("[FP] READY %s %d" % [_pending_id, 1 if _pending_on else 0])
			_settle = -1


func _freeze_particles() -> void:
	particles.speed_scale = 0.0
	trail_particles.speed_scale = 0.0


## What the main viewport's last frame drew: "objects=N primitives=N draws=N".
## Occlusion culling's correct result is the same picture with fewer objects, so
## that arm is judged on these counts (and on its frame being unchanged).
func _render_info() -> String:
	var vp := get_viewport()
	return "objects=%d primitives=%d draws=%d" % [
		vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_OBJECTS_IN_FRAME),
		vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_PRIMITIVES_IN_FRAME),
		vp.get_render_info(Viewport.RENDER_INFO_TYPE_VISIBLE, Viewport.RENDER_INFO_DRAW_CALLS_IN_FRAME)]


## Whether a config applied in this frame's process step and settled for
## `settle` frames would be captured on a frame of phase _phase. The capture is
## the frame drawn settle - 1 frames after this one (the apply frame counts),
## and get_frames_drawn() during the process step numbers the frame about to be
## drawn, on the web and natively alike.
##
## Without this the phase depended on when the harness's first hash change
## landed: one boot frame earlier or later moved every later capture by one
## Halton step, and volumetric fog measured 0.046 or 0.082 from native at
## random. A baked export boots fast enough to straddle that frame; an unbaked
## one, converting shaders during its first frames, did not (work task 16).
func _aligned(settle: int) -> bool:
	return posmod(Engine.get_frames_drawn() + settle - 1, JITTER_PERIOD) == _phase


func _spec_aligned(spec: String) -> bool:
	var parts := spec.split(":")
	var id := parts[0].strip_edges()
	if not FEATURES.has(id):
		return true # nothing is captured ("all"), or _request() reports the id unknown
	_settle_override = int(parts[2]) if parts.size() > 2 else -1
	return _aligned(_settle_for(id))


func _settle_for(id: String) -> int:
	# An override lengthens a feature's settle but never shortens it below what
	# the feature itself needs (sdfgi_camera_move's flight alone is 240 frames).
	return maxi(_settle_override, int(SETTLE_OVERRIDE.get(id, SETTLE_DEFAULT)))


func _request(spec: String) -> void:
	RenderingServer.render_loop_enabled = true
	var parts := spec.split(":")
	var id := parts[0].strip_edges()
	_settle_override = int(parts[2]) if parts.size() > 2 else -1
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
	_settle = _settle_for(id)


## Return every mutable knob to the baseline, so each measurement is independent
## of whatever ran before it. Without this the matrix order would change results.
func _reset() -> void:
	_restore_resources()
	_move_frame = -1
	cam.transform = Transform3D(Basis(), Vector3(0, 2.6, 7.5)).looking_at(Vector3(0, 1.0, 0), Vector3.UP)
	probe_mi.material_override = mat_probe
	mat_array.set_shader_parameter("layer", 0)
	mat_builtins.set_shader_parameter("use_builtins", false)
	mat_points.set_shader_parameter("use_coord", false)

	env.background_mode = Environment.BG_SKY
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.ambient_light_energy = 1.0
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_exposure = 1.0
	env.ssao_enabled = false
	# GTAO: the method is a renderer-wide setting, so every arm but "gtao" runs ASSAO.
	RenderingServer.environment_set_ssao_method(RenderingServer.ENV_SSAO_METHOD_ASSAO)
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
	dir_light.directional_shadow_max_distance = 100.0
	dir_light.shadow_contact_shadows_opacity = 0.0
	dir_light.shadow_contact_shadows_blur = 0.0
	dir_light.light_specular = 0.5
	omni_light.visible = false
	omni_light.shadow_enabled = false
	omni_light.light_projector = null
	omni_light.light_negative = false
	omni_light.omni_shadow_mode = OmniLight3D.SHADOW_CUBE
	spot_light.visible = false
	spot_light.shadow_enabled = false
	spot_light.light_projector = null
	spot_light.light_volumetric_fog_energy = 1.0

	probe.visible = false
	voxel_gi.visible = false
	decal.visible = false
	multimesh_inst.visible = false
	skinned.visible = false
	blendshape_inst.visible = false
	particles.visible = false
	particles.emitting = false
	particles.speed_scale = 1.0
	particle_collider.visible = false
	particle_attractor.visible = false
	trail_particles.visible = false
	trail_particles.emitting = false
	trail_particles.trail_enabled = false
	trail_particles.speed_scale = 1.0
	get_node("Glass").visible = false
	fog_box.visible = false
	fog_ellipsoid.visible = false
	fog_shaded.visible = false
	occ_wall.visible = false
	occluder.visible = false
	for b: MeshInstance3D in occluded:
		b.visible = false
	lightmap_room.visible = false
	if lightmap_gi and lightmap_gi.light_data:
		lightmap_gi.light_data = null
	compositor_effect.enabled = false
	nan_quad.visible = false
	nan_quad.transform = NAN_QUAD_XFORM
	glare_sphere.visible = false
	cam.attributes = cam_attrs

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
	cam_attrs.white_balance_temperature = 6500.0
	cam_attrs.white_balance_tint = 0.0

	var vp := get_viewport()
	vp.msaa_3d = Viewport.MSAA_DISABLED
	vp.screen_space_aa = Viewport.SCREEN_SPACE_AA_DISABLED
	vp.use_taa = false
	vp.use_debanding = false
	vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
	vp.scaling_3d_scale = 1.0
	vp.mesh_lod_threshold = 1.0
	vp.texture_mipmap_bias = 0.0
	vp.use_occlusion_culling = false


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
		"fog_volume":
			# Box and ellipsoid FogVolumes with a FogMaterial; the environment's
			# own density is 0, so without the volumes there is no fog at all.
			env.volumetric_fog_enabled = true
			env.volumetric_fog_density = 0.0
			fog_box.visible = on
			fog_ellipsoid.visible = on
		"fog_volume_shader":
			# A FogVolume with a user `shader_type fog` material.
			env.volumetric_fog_enabled = true
			env.volumetric_fog_density = 0.0
			fog_shaded.visible = on
		"lightmap_gi":
			lightmap_room.visible = true
			if on and lightmap_gi:
				lightmap_gi.light_data = lightmap_gi.get_meta("baked")
			cam.transform = Transform3D(Basis(), Vector3(-60, 1.7, 4.6)).looking_at(Vector3(-60, 1.0, -1.0), Vector3.UP)
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
		"gtao":
			# The ssao arm's settings with GTAO computing the AO: off is no AO at all.
			env.ssao_enabled = on
			env.ssao_intensity = 6.0
			env.ssao_radius = 2.0
			RenderingServer.environment_set_ssao_method(RenderingServer.ENV_SSAO_METHOD_GTAO)
		"ssil":
			env.ssil_enabled = on
			env.ssil_intensity = 4.0
			env.ssil_radius = 4.0
		"sdfgi":
			env.sdfgi_enabled = on
			env.sdfgi_energy = 4.0
			env.sdfgi_cascades = 2
		"sdfgi_camera_move":
			# Parity only: SDFGI must look the same after the camera has flown
			# away and back as if it had never moved. Moving scrolls every
			# cascade, which re-voxelizes and re-clears 3D textures; on WebGPU
			# those clears used to cover only z-slice 0 and run out of order,
			# leaving stale voxels behind (25/255 off the native frame).
			env.sdfgi_enabled = true
			env.sdfgi_energy = 4.0
			env.sdfgi_cascades = 2
			if on:
				_cam_base = cam.transform
				_move_frame = 0

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
			omni_light.omni_shadow_mode = OmniLight3D.SHADOW_DUAL_PARABOLOID
		"omni_shadow_cube":
			# The default omni shadow mode. WebGPU used to force dual-paraboloid
			# whatever the light asked for, and cube shadows rendered nothing
			# there once that stopped (the cube-to-atlas copy read a blank
			# fallback instead of the depth cubemap).
			omni_light.visible = true
			omni_light.shadow_enabled = on
			omni_light.omni_shadow_mode = OmniLight3D.SHADOW_CUBE
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
			# Baked data is loaded, not produced at runtime: a shipped project
			# bakes in the editor, and baking inside the matrix competed with
			# the settle window and made the measurement non-reproducible
			# (1.051 one run, 0.040 the next). make_bake_assets.gd regenerates
			# voxel_gi_data.res when the static geometry changes.
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
		"depth_texture":
			# DEPTH_TEXTURE read zeros on WebGPU: the depth copy's source was
			# declared as a float texture, so the depth buffer was never bound.
			probe_mi.material_override = mat_depth if on else mat_probe
		"mipmap_bias":
			# The samplers' LOD bias (texture_mipmap_bias, TAA, FSR), which WebGPU
			# samplers cannot carry and the shader translation now applies.
			mat_probe.albedo_texture = tex_checker_mips
			vp.texture_mipmap_bias = 2.0 if on else 0.0
		"texture_array":
			probe_mi.material_override = mat_array
			mat_array.set_shader_parameter("layer", 2 if on else 0)
		"shader_builtins":
			probe_mi.material_override = mat_builtins
			mat_builtins.set_shader_parameter("use_builtins", on)
		"point_coord":
			probe_mi.material_override = mat_points
			mat_points.set_shader_parameter("use_coord", on)
		"stencil_outline":
			# BaseMaterial3D's outline preset: a stencil-tested, grown second pass
			# drawn only where the probe did not write the stencil.
			mat_probe.stencil_mode = BaseMaterial3D.STENCIL_MODE_OUTLINE if on else BaseMaterial3D.STENCIL_MODE_DISABLED
			mat_probe.stencil_color = Color(1.0, 0.55, 0.1)
			mat_probe.stencil_outline_thickness = 0.12
		"stencil_xray":
			# The x-ray preset: the probe, hidden behind occ_wall, is drawn
			# through it where the wall's depth fails it.
			occ_wall.visible = true
			mat_probe.stencil_mode = BaseMaterial3D.STENCIL_MODE_XRAY if on else BaseMaterial3D.STENCIL_MODE_DISABLED
			mat_probe.stencil_color = Color(0.2, 0.8, 1.0)
		"bent_normal":
			# Bent normals steer the indirect (here: sky ambient) light and the
			# specular occlusion, not direct light, so the sun is off and the
			# ambient up: the probe is lit by the sky alone, and bending its
			# normals up or down picks the blue top or the brown ground.
			dir_light.visible = false
			env.ambient_light_energy = 3.0
			mat_probe.albedo_texture = null
			mat_probe.albedo_color = Color(0.9, 0.9, 0.9)
			mat_probe.roughness = 1.0
			mat_probe.bent_normal_enabled = on
			mat_probe.bent_normal_texture = tex_bent

		# ---- mesh pipeline ----
		"multimesh":
			multimesh_inst.visible = on
		"skeletal_animation":
			skinned.visible = true
			skeleton.set_bone_pose_rotation(1, Quaternion(Vector3(0, 0, 1), 1.3) if on else Quaternion())
		"blend_shapes":
			blendshape_inst.visible = true
			blendshape_inst.set_blend_shape_value(0, 1.0 if on else 0.0)
		"mesh_lod":
			vp.mesh_lod_threshold = 64.0 if on else 0.0
		"visibility_range":
			fade_inst.visibility_range_end = 2.0 if on else 0.0
		"occlusion_culling":
			# Judged on the viewport's object count, not on the frame: the
			# correct frame is the same with and without (features.mjs maxDelta).
			occ_wall.visible = true
			occluder.visible = true
			for b: MeshInstance3D in occluded:
				b.visible = true
			vp.use_occlusion_culling = on

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
			trail_particles.visible = true
			trail_particles.emitting = true
			trail_particles.trail_enabled = on

		# ---- post / AA / scaling ----
		"msaa":
			vp.msaa_3d = Viewport.MSAA_4X if on else Viewport.MSAA_DISABLED
		"taa":
			vp.use_taa = on
		"fxaa":
			vp.screen_space_aa = (Viewport.SCREEN_SPACE_AA_FXAA if on
				else Viewport.SCREEN_SPACE_AA_DISABLED)
		"smaa":
			vp.screen_space_aa = (Viewport.SCREEN_SPACE_AA_SMAA if on
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
		"dof_nan":
			# The far blur both ways; "on" adds one NaN source in it. The DOF passes must keep it to
			# the quad's own pixels: before they treated a non-finite color as black, NaN * 0 = NaN in
			# the filter spread it over the whole bokeh hexagon (webgpu_notes/TASKS.md §3).
			cam_attrs.dof_blur_far_enabled = true
			cam_attrs.dof_blur_far_distance = 6.0
			cam_attrs.dof_blur_far_transition = 1.0
			cam_attrs.dof_blur_amount = 0.3
			nan_quad.visible = on
		"auto_exposure":
			cam_attrs.auto_exposure_enabled = on
			cam_attrs.auto_exposure_scale = 0.2
		# The *_nan arms: the effect on both ways, "on" adds the dof_nan quad, one NaN source. Each
		# effect reads the scene color through finite_or_zero() (finite_inc.glsl), so the frame
		# changes by the quad's own pixels; before, glow carried it over its radius (or, where min()
		# drops a NaN, lit a blob at the luminance cap), SSR reflected and blurred it, and auto
		# exposure averaged it into the luminance of the whole frame (webgpu_notes/TASKS.md §3).
		"glow_nan":
			env.glow_enabled = true
			env.glow_intensity = 1.6
			env.glow_bloom = 0.5
			env.glow_hdr_threshold = 0.6
			nan_quad.visible = on
		"ssr_nan":
			# Above the floor in front of the sphere, so its mirror image is on screen in the floor.
			env.ssr_enabled = true
			env.ssr_max_steps = 64
			nan_quad.transform = Transform3D(Basis().scaled(Vector3(0.3, 0.3, 0.3)), Vector3(1.6, 0.7, 2.0))
			nan_quad.visible = on
		"auto_exposure_nan":
			cam_attrs.auto_exposure_enabled = true
			cam_attrs.auto_exposure_scale = 0.2
			nan_quad.visible = on
		"white_balance":
			# CameraAttributes white balance (engine addition): a tungsten setting turns the daylight-lit
			# scene blue; the small tint exercises the off-locus path.
			cam_attrs.white_balance_temperature = 3200.0 if on else 6500.0
			cam_attrs.white_balance_tint = 30.0 if on else 0.0
		"compositor_effect":
			# fp_compositor_effect.gd: a compute shader inverting part of the frame.
			compositor_effect.enabled = on
		# ---- camera effects (CameraAttributes / Environment engine additions) ----
		"lens_optics":
			# Vignetting, barrel distortion (auto-cropped) and lateral chromatic aberration (TASKS.md 4.52).
			cam_attrs.vignetting_enabled = on
			cam_attrs.vignetting_strength = 1.0
			cam_attrs.distortion_enabled = on
			cam_attrs.distortion_k1 = -0.15
			cam_attrs.distortion_k2 = 0.02
			cam_attrs.chromatic_aberration_enabled = on
			cam_attrs.chromatic_aberration_strength = 4.0
		"bokeh_shape":
			# The far blur both ways; on, a 5-blade aperture and the cat's eye replace the project's hexagon
			# (the circular gather with the shape test, TASKS.md 4.54). The glare sphere's bokeh shows the shape.
			glare_sphere.visible = true
			cam_attrs.dof_blur_far_enabled = true
			cam_attrs.dof_blur_far_distance = 6.0
			cam_attrs.dof_blur_far_transition = 1.0
			cam_attrs.dof_blur_amount = 0.3
			cam_attrs.aperture_enabled = on
			cam_attrs.aperture_blade_count = 5
			cam_attrs.aperture_blade_curvature = 0.0
			cam_attrs.aperture_rotation = 10.0
			cam_attrs.cats_eye_enabled = on
			cam_attrs.cats_eye_strength = 1.5
		"longitudinal_ca":
			# The physical camera's DOF both ways; on, red and blue get their own circles of confusion.
			_use_physical_camera()
			glare_sphere.visible = true
			cam_attrs_physical.longitudinal_chromatic_aberration_enabled = on
			cam_attrs_physical.longitudinal_chromatic_aberration_strength = 8.0
		"tilt_shift":
			# The physical camera's DOF both ways; on, the lens tilts and the plane of focus with it.
			_use_physical_camera()
			cam_attrs_physical.tilt_shift_enabled = on
			cam_attrs_physical.tilt_shift_tilt = -6.0
		"starburst":
			glare_sphere.visible = true
			cam_attrs.starburst_enabled = on
			cam_attrs.starburst_strength = 40.0
		"halation":
			glare_sphere.visible = true
			cam_attrs.halation_enabled = on
			cam_attrs.halation_strength = 4.0
			cam_attrs.halation_radius = 2.0
		"lens_flare":
			glare_sphere.visible = true
			env.lens_flare_enabled = on
			env.lens_flare_intensity = 20.0
		"veiling_glare_diffusion":
			glare_sphere.visible = true
			env.veiling_glare_enabled = on
			env.veiling_glare_strength = 3.0
			env.diffusion_filter_enabled = on
			env.diffusion_filter_strength = 1.0
		"camera_lut":
			cam_attrs.lut_enabled = on
			cam_attrs.lut_texture = tex_lut3d
		"in_camera_processing":
			cam_attrs.noise_reduction_enabled = on
			cam_attrs.sharpening_enabled = on
			cam_attrs.sharpening_amount = 2.0
		"micro_shadows":
			# Normal and AO maps in both frames: the delta is the micro shadowing alone.
			mat_probe.normal_enabled = true
			mat_probe.normal_texture = tex_normal
			mat_probe.normal_scale = 4.0
			mat_probe.ao_enabled = true
			mat_probe.ao_texture = tex_ao
			mat_probe.uv1_scale = Vector3(3, 3, 3)
			env.micro_shadows_enabled = on
			env.micro_shadows_opacity = 1.0
		"local_exposure":
			# The bright emissive sphere in both frames, so there are highlights to compress; strong contrasts so the
			# grid's slicing (not only the global level) shows in the delta.
			glare_sphere.visible = true
			cam_attrs.local_exposure_enabled = on
			cam_attrs.local_exposure_highlight_contrast = 0.4
			cam_attrs.local_exposure_shadow_contrast = 0.6
			cam_attrs.local_exposure_blurred_luminance_blend = 0.3
		_:
			return false
	return true
