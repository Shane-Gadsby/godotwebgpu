/**
 * The Forward+ feature matrix: one entry per feature the WebGPU port is
 * expected to render.
 *
 * `minDelta` is the floor on mean absolute pixel difference (0-255) between the
 * feature off and on. It is the whole point of the suite: the failure mode this
 * port keeps hitting is a feature silently doing nothing -- a shader that fails
 * to convert, a texture that fails to allocate, an effect skipped behind a
 * capability check -- which produces no console error and looks exactly like a
 * correct frame unless you compare against the same frame without it.
 *
 * Floors are set from a measured run (see README, "Recalibrating"), at roughly
 * a third of the observed delta, so normal driver and GPU variation cannot trip
 * them but a feature dropping to zero effect always does. They are deliberately
 * NOT tight upper bounds: this suite answers "does this feature do something of
 * about the right magnitude", not "does this frame match a golden image". A
 * golden-image suite would need regenerating on every legitimate lighting
 * change and would say nothing about why it moved.
 *
 * `id` must match an arm of fp_scene.gd's `_apply()` and an entry in its
 * FEATURES list; run_forward_plus.mjs asserts all three agree, so a feature
 * added on one side and not the other fails loudly.
 *
 * The parity mode (run_forward_plus.mjs --parity) also compares every captured
 * frame against a native Vulkan render of the same configuration, and fails a
 * feature whose WebGPU frame differs by more than PARITY_TOLERANCE (mean
 * absolute difference, 0-255) -- the check that catches a feature that does
 * something, but the wrong thing. Two optional fields serve it:
 *   parityOnly: true   -- the arm exists for the parity comparison alone; its
 *                         correct "on" frame equals its "off" frame, so the
 *                         delta test skips it.
 *   parityTol: N       -- a larger tolerance for a feature whose WebGPU frame
 *                         legitimately differs, with the reason next to it.
 *                         Never raise one to make a regression pass.
 *
 * Two more serve an arm whose correct result is the same picture (occlusion
 * culling: fewer objects drawn, nothing visible changes):
 *   maxDelta: N        -- the off/on frames must differ by at most this much;
 *                         replaces the minDelta floor.
 *   infoDrop: {key: N} -- the fixture reports what each captured frame drew
 *                         (`[FP] INFO objects=.. primitives=.. draws=..`, from
 *                         Viewport.get_render_info; `<id>_<0|1>.info` natively),
 *                         and "on" must draw at least N fewer of each key than
 *                         "off". Checked on the web, and in parity mode on the
 *                         native reference too, so a fixture that stopped
 *                         occluding anything fails instead of passing as "same".
 *
 *   knownFail: 'why'   -- a failure on the web that is understood and out of
 *                         this port's reach, with the reason. The arm still runs
 *                         and reports KNOWN without failing the run; a native
 *                         side that no longer does what the arm needs still
 *                         fails, and an arm that starts passing reports XPASS
 *                         and fails, so the marker is removed with the bug.
 *
 *   needsRaycast: 'why' -- the arm needs the raycast module (occlusion culling).
 *                         Against a template built without it (the web
 *                         default), run_forward_plus.mjs turns this into
 *                         knownFail; against one built with it, a normal arm.
 */

// Default parity tolerance: well above what identical work measures across the
// two backends (0.002-0.006 on the reference RTX 4080), well below a missing
// or wrong effect (the bugs this mode found measured 0.06-25).
export const PARITY_TOLERANCE = 0.05;

export const CATEGORIES = {
  environment: 'Environment',
  lighting: 'Lights and shadows',
  gi: 'Global illumination',
  material: 'Materials',
  mesh: 'Mesh pipeline',
  particles: 'Particles',
  post: 'Post-processing, AA and scaling',
  camera: 'Camera effects (CameraAttributes, Environment)',
};

export const FEATURES = [
  // ---- environment ----
  { id: 'ambient_light', cat: 'environment', label: 'Ambient light (constant colour)', minDelta: 12.6 },
  { id: 'reflected_light', cat: 'environment', label: 'Reflected light from sky', minDelta: 4.02 },
  { id: 'sky', cat: 'environment', label: 'Sky background (procedural)', minDelta: 14.8 },
  { id: 'tonemap', cat: 'environment', label: 'Tonemapping (ACES vs linear)', minDelta: 7.37 },
  { id: 'tonemap_exposure', cat: 'environment', label: 'Tonemap exposure', minDelta: 15.5 },
  { id: 'adjustments', cat: 'environment', label: 'Adjustments (brightness/contrast/saturation)', minDelta: 20.5 },
  { id: 'color_correction', cat: 'environment', label: 'Colour correction LUT', minDelta: 23.6 },
  { id: 'fog', cat: 'environment', label: 'Depth fog', minDelta: 17.3 },
  { id: 'fog_height', cat: 'environment', label: 'Height fog', minDelta: 17.2 },
  { id: 'fog_aerial_perspective', cat: 'environment', label: 'Fog aerial perspective', minDelta: 0.896 },
  { id: 'volumetric_fog', cat: 'environment', label: 'Volumetric fog', minDelta: 14.4 },
  { id: 'fog_volume', cat: 'environment', label: 'FogVolume nodes (box and ellipsoid, FogMaterial)', minDelta: 1.72 },
  { id: 'fog_volume_shader', cat: 'environment', label: 'FogVolume with a custom fog shader', minDelta: 1.89 },
  { id: 'glow', cat: 'environment', label: 'Glow / bloom', minDelta: 14.4 },
  { id: 'glow_map', cat: 'environment', label: 'Glow map', minDelta: 0.7 },
  { id: 'ssr', cat: 'environment', label: 'Screen-space reflections', minDelta: 0.394 },
  { id: 'ssao', cat: 'environment', label: 'Screen-space ambient occlusion', minDelta: 0.555 },
  { id: 'ssil', cat: 'environment', label: 'Screen-space indirect lighting', minDelta: 0.579 },
  { id: 'sdfgi', cat: 'environment', label: 'SDFGI', minDelta: 11.7,
    // SDFGI voxelization ORs facing bits into a shared grid; WGSL has no image
    // atomics, so WebGPU does a plain read-modify-write and concurrent
    // fragments can drop each other's bits (measured 0.03).
    parityTol: 0.08 },
  { id: 'sdfgi_camera_move', cat: 'environment', label: 'SDFGI after the camera moves away and back', minDelta: 0, parityOnly: true,
    parityTol: 0.08 },

  // ---- lights and shadows ----
  { id: 'directional_light', cat: 'lighting', label: 'Directional light', minDelta: 10.7 },
  { id: 'directional_shadow', cat: 'lighting', label: 'Directional shadow', minDelta: 0.734 },
  { id: 'directional_shadow_splits', cat: 'lighting', label: 'Directional shadow splits (CSM)', minDelta: 0.207 },
  { id: 'omni_light', cat: 'lighting', label: 'Omni light', minDelta: 4.32 },
  { id: 'omni_shadow', cat: 'lighting', label: 'Omni shadow (dual paraboloid)', minDelta: 0.222 },
  { id: 'omni_shadow_cube', cat: 'lighting', label: 'Omni shadow (cube)', minDelta: 0.222 },
  { id: 'spot_light', cat: 'lighting', label: 'Spot light', minDelta: 1.27 },
  { id: 'spot_shadow', cat: 'lighting', label: 'Spot shadow', minDelta: 0.415 },
  { id: 'light_projector', cat: 'lighting', label: 'Light projector texture', minDelta: 1.11 },
  { id: 'soft_shadows', cat: 'lighting', label: 'Soft shadows (PCSS)', minDelta: 0.517 },
  { id: 'shadow_blur', cat: 'lighting', label: 'Shadow blur', minDelta: 1.65 },
  { id: 'contact_shadows', cat: 'lighting', label: 'Screen-space contact shadows (4.8)', minDelta: 0.0346 },
  { id: 'light_negative', cat: 'lighting', label: 'Negative light', minDelta: 11.5 },
  { id: 'light_specular', cat: 'lighting', label: 'Light specular contribution', minDelta: 0.14 },
  { id: 'micro_shadows', cat: 'lighting', label: 'Micro shadows from AO and normal maps (Environment)', minDelta: 0.229 },

  // ---- global illumination ----
  { id: 'reflection_probe', cat: 'gi', label: 'Reflection probe', minDelta: 4.39 },
  { id: 'voxel_gi', cat: 'gi', label: 'VoxelGI', minDelta: 2.58 },
  { id: 'decal', cat: 'gi', label: 'Decal', minDelta: 0.447 },
  // A committed bake (lightmap_room.*, bake_lightmap.sh); the room's light is static, so "on" takes its direct light too
  // from the lightmap.
  { id: 'lightmap_gi', cat: 'gi', label: 'LightmapGI (baked, static light)', minDelta: 4.21 },

  // ---- materials ----
  { id: 'normal_map', cat: 'material', label: 'Normal map', minDelta: 0.623 },
  { id: 'emission', cat: 'material', label: 'Emission', minDelta: 1.11 },
  { id: 'rim', cat: 'material', label: 'Rim lighting', minDelta: 0.0739 },
  { id: 'clearcoat', cat: 'material', label: 'Clearcoat', minDelta: 0.0978 },
  { id: 'anisotropy', cat: 'material', label: 'Anisotropy', minDelta: 0.162 },
  { id: 'subsurface_scattering', cat: 'material', label: 'Subsurface scattering', minDelta: 0.216 },
  { id: 'backlight', cat: 'material', label: 'Backlight', minDelta: 0.166 },
  { id: 'refraction', cat: 'material', label: 'Refraction', minDelta: 0.15 },
  { id: 'heightmap', cat: 'material', label: 'Heightmap / parallax', minDelta: 0.574 },
  { id: 'triplanar', cat: 'material', label: 'Triplanar mapping', minDelta: 0.537 },
  { id: 'transparency', cat: 'material', label: 'Alpha blending', minDelta: 1.27 },
  { id: 'alpha_scissor', cat: 'material', label: 'Alpha scissor', minDelta: 1.15 },
  { id: 'alpha_hash', cat: 'material', label: 'Alpha hash', minDelta: 1.03 },
  { id: 'proximity_fade', cat: 'material', label: 'Proximity fade', minDelta: 0.19 },
  { id: 'distance_fade', cat: 'material', label: 'Distance fade', minDelta: 1.1 },
  { id: 'depth_texture', cat: 'material', label: 'DEPTH_TEXTURE in a material', minDelta: 1.4 },
  { id: 'mipmap_bias', cat: 'material', label: 'Mipmap LOD bias (texture_mipmap_bias)', minDelta: 0.148 },
  { id: 'texture_array', cat: 'material', label: 'Texture array indexed at runtime', minDelta: 1.2 },
  // L8, LA8, RGBA4444, RGB565, R8 and RG8 images on a material against the same images expanded to RGBA8: Vulkan
  // shows the first four through a view swizzle, which WebGPU does not have (task 58, TASKS.md 2.43). Natively off
  // and on are the same picture, so parity only.
  { id: 'image_formats', cat: 'material', label: 'Image formats Vulkan swizzles (L8, LA8, RGBA4444, RGB565, R8, RG8)', minDelta: 0, parityOnly: true },
  { id: 'shader_builtins', cat: 'material', label: 'isnan/isinf/textureQueryLod in a material', minDelta: 0.028 },
  { id: 'point_coord', cat: 'material', label: 'POINT_COORD without POINT_SIZE (drawn as points)', minDelta: 0.01 },
  { id: 'stencil_outline', cat: 'material', label: 'Stencil outline (BaseMaterial3D stencil_mode)', minDelta: 0.319 },
  { id: 'stencil_xray', cat: 'material', label: 'Stencil x-ray (BaseMaterial3D stencil_mode)', minDelta: 1.23 },
  { id: 'bent_normal', cat: 'material', label: 'Bent normal map', minDelta: 0.225 },

  // ---- mesh pipeline ----
  { id: 'multimesh', cat: 'mesh', label: 'MultiMesh instancing', minDelta: 1.72 },
  { id: 'skeletal_animation', cat: 'mesh', label: 'Skeletal animation (GPU skinning)', minDelta: 0.665 },
  { id: 'blend_shapes', cat: 'mesh', label: 'Blend shapes', minDelta: 0.861 },
  { id: 'mesh_lod', cat: 'mesh', label: 'Mesh LOD', minDelta: 0.504 },
  { id: 'visibility_range', cat: 'mesh', label: 'Visibility range', minDelta: 0.734 },
  // Correct result: the same frame, fewer objects. Natively 29 -> 8 objects (20 boxes and the probe sphere behind the
  // wall), the frames identical to 0.000.
  { id: 'occlusion_culling', cat: 'mesh', label: 'Occlusion culling (OccluderInstance3D)', minDelta: 0, maxDelta: 0.02,
    infoDrop: { objects: 15 },
    // A web template has an occlusion culler only when built with module_raycast_enabled=yes (Embree; off by default in
    // platform/web/detect.py, as upstream: GH-70621). Against one built without it this arm is KNOWN with this reason
    // (run_forward_plus.mjs checks the export's wasm), against one built with it a normal arm: 29 -> 8, as natively.
    needsRaycast: 'this web template was built without the raycast module (module_raycast_enabled=no, the web ' +
      'default in platform/web/detect.py, upstream GH-70621), so there is no occlusion culler: the frame is right, ' +
      'nothing is culled (29 objects off and on). Build the template with module_raycast_enabled=yes.' },

  // ---- particles ----
  { id: 'gpu_particles', cat: 'particles', label: 'GPU particles', minDelta: 3.14 },
  { id: 'particle_collision', cat: 'particles', label: 'Particle collision', minDelta: 3.02 },
  { id: 'particle_attractor', cat: 'particles', label: 'Particle attractor', minDelta: 3.22 },
  { id: 'particle_trails', cat: 'particles', label: 'Particle trails (RibbonTrailMesh)', minDelta: 2.42 },

  // ---- post-processing, AA and scaling ----
  { id: 'msaa', cat: 'post', label: 'MSAA 4x', minDelta: 0.0673 },
  { id: 'taa', cat: 'post', label: 'TAA', minDelta: 0.15 },
  { id: 'fxaa', cat: 'post', label: 'FXAA', minDelta: 0.0812 },
  { id: 'smaa', cat: 'post', label: 'SMAA', minDelta: 0.081 },
  { id: 'debanding', cat: 'post', label: 'Debanding', minDelta: 0.0823 },
  { id: 'scaling_3d', cat: 'post', label: '3D resolution scaling (bilinear)', minDelta: 0.373 },
  { id: 'fsr2', cat: 'post', label: 'FSR2 upscaling', minDelta: 0.358,
    // Measured 0.142: one or two levels of noise on 29% of pixels, 0.24% off
    // by more than 2. Not fp16 or the lookup tables: native Vulkan without
    // FFX_HALF renders the same frame bit for bit, and with WebGPU's
    // half-float tables within 0.000. It was 0.55 (and visibly softer) while FSR2 read its reactive
    // mask through an alpha-swizzled view WebGPU cannot swizzle (task 17).
    parityTol: 0.3 },
  // FSR2 while the camera pans over the sky (motion vectors derived from depth there), parity only (task 58).
  { id: 'fsr2_camera_move', cat: 'post', label: 'FSR2 with the camera panning across the sky', minDelta: 0, parityOnly: true,
    parityTol: 0.3 },
  { id: 'dof_far', cat: 'post', label: 'Depth of field (far)', minDelta: 2.42 },
  { id: 'dof_near', cat: 'post', label: 'Depth of field (near)', minDelta: 0.617 },
  // A 2x2 px quad writing NaN inside the far blur. The DOF passes treat a non-finite color as black,
  // so only the quad and a faint darkening around it change; before, NaN * 0 = NaN in the bokeh filter
  // blanked a whole hexagon (1.07 natively; 0.0013 after). Off and on both have the far blur.
  { id: 'dof_nan', cat: 'post', label: 'Depth of field keeps a NaN pixel to itself', minDelta: 0, maxDelta: 0.05 },
  // The same quad through glow, SSR and auto exposure, each on both ways (work task 28). Each reads
  // the scene color through finite_or_zero(), so only the quad's own pixels change. Before: glow
  // spread it over its radius, SSR reflected and blurred it, auto exposure averaged it into the
  // luminance of the whole frame.
  { id: 'glow_nan', cat: 'post', label: 'Glow keeps a NaN pixel to itself', minDelta: 0, maxDelta: 0.05 },
  { id: 'ssr_nan', cat: 'post', label: 'SSR keeps a NaN pixel to itself', minDelta: 0, maxDelta: 0.05 },
  { id: 'auto_exposure_nan', cat: 'post', label: 'Auto exposure keeps a NaN pixel to itself', minDelta: 0, maxDelta: 0.05 },
  { id: 'auto_exposure', cat: 'post', label: 'Auto exposure', minDelta: 11.6 },
  // CameraAttributes white balance, an engine addition (3200 K, tint 30 against the 6500 K default). Native
  // lavapipe measured 48.1 (2026-10-26), floor at a third of it.
  { id: 'white_balance', cat: 'post', label: 'White balance (CameraAttributes)', minDelta: 16.0 },
  // User RD code (fp_compositor_effect.gd), but the path under it is engine: run-time GLSL compile, the driver's
  // conversion, a read-write storage image on the scene's colour target, the compositor callback.
  { id: 'compositor_effect', cat: 'post', label: 'CompositorEffect (compute shader on the colour buffer)', minDelta: 6.98 },
  // Camera effects, engine additions (TASKS.md 4.52-4.55, work task 44); the deterministic ones only (grain, zebras
  // and motion-dependent effects change every frame). Floors at a third of the native Vulkan delta
  // (RTX 4080 SUPER, 640x360, settle 240, 2026-10-28).
  { id: 'lens_optics', cat: 'camera', label: 'Lens optics: vignetting, distortion, lateral CA (CameraAttributes)', minDelta: 6.09 },
  { id: 'bokeh_shape', cat: 'camera', label: "Aperture blades and cat's eye bokeh (CameraAttributes)", minDelta: 0.822 },
  { id: 'longitudinal_ca', cat: 'camera', label: 'Longitudinal chromatic aberration in the DOF (CameraAttributesPhysical)', minDelta: 0.136 },
  { id: 'tilt_shift', cat: 'camera', label: 'Tilt-shift plane of focus (CameraAttributesPhysical)', minDelta: 0.591 },
  { id: 'starburst', cat: 'camera', label: 'Diffraction starburst (CameraAttributes)', minDelta: 0.954 },
  { id: 'halation', cat: 'camera', label: 'Halation (CameraAttributes)', minDelta: 0.312 },
  { id: 'lens_flare', cat: 'camera', label: 'Lens flare ghosts (Environment)', minDelta: 0.373 },
  { id: 'veiling_glare_diffusion', cat: 'camera', label: 'Veiling glare and diffusion filter (Environment)', minDelta: 2.06 },
  { id: 'camera_lut', cat: 'camera', label: 'Camera 3D LUT (CameraAttributes)', minDelta: 3.62 },
  { id: 'in_camera_processing', cat: 'camera', label: 'In-camera noise reduction and sharpening (CameraAttributes)', minDelta: 0.175 },
  // GTAO (engine addition): the ssao arm's settings with rendering/environment/ssao/method = GTAO.
  { id: 'gtao', cat: 'environment', label: 'GTAO ambient occlusion (ssao method GTAO)', minDelta: 1.39 },
  // Custom tonemapper (engine addition): non-default slope/toe/shoulder/clips against the fixture's Filmic.
  { id: 'tonemap_custom', cat: 'environment', label: 'Custom tonemapper (toe, shoulder, slope, clips)', minDelta: 9.8 },
  // CameraAttributes local exposure (engine addition, work task 57): highlight contrast 0.4, shadow 0.6, the bright
  // sphere in both frames. Measured 11.39 on WebGPU with --parity (native diff 0.002 off / 0.004 on, so native's delta is
  // the same), RTX 4080 SUPER, 2026-10-28; floor at a third.
  { id: 'local_exposure', cat: 'camera', label: 'Local exposure, bilateral grid (CameraAttributes)', minDelta: 3.8 },
];

/**
 * Forward+ features deliberately NOT in the matrix above, with the reason.
 *
 * This list is as important as the matrix: it is the difference between "this
 * feature is known not to be covered" and "nobody ever thought about it". Each
 * entry must say why, and anything whose reason stops being true belongs in the
 * matrix. Adding a feature to this list is a decision to be reviewed, not a
 * place to park work.
 */
export const UNCOVERED = [
  { id: 'multiview_xr', why: 'WebGPU has no multiview; the driver reports SUPPORTS_MULTIVIEW false and the renderer takes the single-view path. Nothing to compare.' },
  { id: 'vrs', why: 'WebGPU has no variable shading rate; SUPPORTS_ATTACHMENT_VRS is false by design.' },
  { id: 'msaa_2x_8x', why: 'WebGPU allows only 1 and 4 samples per pixel; MSAA 2x and 8x cannot exist on it (the msaa arm covers 4x).' },
];

export const byId = Object.fromEntries(FEATURES.map((f) => [f.id, f]));
