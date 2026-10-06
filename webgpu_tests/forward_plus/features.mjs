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
 */

export const CATEGORIES = {
  environment: 'Environment',
  lighting: 'Lights and shadows',
  gi: 'Global illumination',
  material: 'Materials',
  mesh: 'Mesh pipeline',
  particles: 'Particles',
  post: 'Post-processing, AA and scaling',
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
  { id: 'glow', cat: 'environment', label: 'Glow / bloom', minDelta: 14.4 },
  { id: 'glow_map', cat: 'environment', label: 'Glow map', minDelta: 10.4 },
  { id: 'ssr', cat: 'environment', label: 'Screen-space reflections', minDelta: 0.394 },
  { id: 'ssao', cat: 'environment', label: 'Screen-space ambient occlusion', minDelta: 0.555 },
  { id: 'ssil', cat: 'environment', label: 'Screen-space indirect lighting', minDelta: 0.579 },
  { id: 'sdfgi', cat: 'environment', label: 'SDFGI', minDelta: 11.7 },

  // ---- lights and shadows ----
  { id: 'directional_light', cat: 'lighting', label: 'Directional light', minDelta: 10.7 },
  { id: 'directional_shadow', cat: 'lighting', label: 'Directional shadow', minDelta: 0.734 },
  { id: 'directional_shadow_splits', cat: 'lighting', label: 'Directional shadow splits (CSM)', minDelta: 0.207 },
  { id: 'omni_light', cat: 'lighting', label: 'Omni light', minDelta: 4.32 },
  { id: 'omni_shadow', cat: 'lighting', label: 'Omni shadow (dual paraboloid)', minDelta: 0.222 },
  { id: 'spot_light', cat: 'lighting', label: 'Spot light', minDelta: 1.27 },
  { id: 'spot_shadow', cat: 'lighting', label: 'Spot shadow', minDelta: 0.415 },
  { id: 'light_projector', cat: 'lighting', label: 'Light projector texture', minDelta: 1.11 },
  { id: 'soft_shadows', cat: 'lighting', label: 'Soft shadows (PCSS)', minDelta: 0.517 },
  { id: 'shadow_blur', cat: 'lighting', label: 'Shadow blur', minDelta: 1.65 },
  { id: 'contact_shadows', cat: 'lighting', label: 'Screen-space contact shadows (4.8)', minDelta: 0.0346 },
  { id: 'light_negative', cat: 'lighting', label: 'Negative light', minDelta: 11.5 },
  { id: 'light_specular', cat: 'lighting', label: 'Light specular contribution', minDelta: 0.14 },

  // ---- global illumination ----
  { id: 'reflection_probe', cat: 'gi', label: 'Reflection probe', minDelta: 4.39 },
  { id: 'voxel_gi', cat: 'gi', label: 'VoxelGI', minDelta: 2.58 },
  { id: 'decal', cat: 'gi', label: 'Decal', minDelta: 0.447 },

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
  { id: 'proximity_fade', cat: 'material', label: 'Proximity fade', minDelta: 1.09 },
  { id: 'distance_fade', cat: 'material', label: 'Distance fade', minDelta: 1.1 },

  // ---- mesh pipeline ----
  { id: 'multimesh', cat: 'mesh', label: 'MultiMesh instancing', minDelta: 1.72 },
  { id: 'blend_shapes', cat: 'mesh', label: 'Blend shapes', minDelta: 0.861 },
  { id: 'mesh_lod', cat: 'mesh', label: 'Mesh LOD', minDelta: 0.504 },
  { id: 'visibility_range', cat: 'mesh', label: 'Visibility range', minDelta: 0.734 },

  // ---- particles ----
  { id: 'gpu_particles', cat: 'particles', label: 'GPU particles', minDelta: 3.14 },
  { id: 'particle_collision', cat: 'particles', label: 'Particle collision', minDelta: 3.02 },
  { id: 'particle_attractor', cat: 'particles', label: 'Particle attractor', minDelta: 3.22 },

  // ---- post-processing, AA and scaling ----
  { id: 'msaa', cat: 'post', label: 'MSAA 4x', minDelta: 0.0673 },
  { id: 'taa', cat: 'post', label: 'TAA', minDelta: 0.15 },
  { id: 'fxaa', cat: 'post', label: 'FXAA', minDelta: 0.0812 },
  { id: 'debanding', cat: 'post', label: 'Debanding', minDelta: 0.0823 },
  { id: 'scaling_3d', cat: 'post', label: '3D resolution scaling (bilinear)', minDelta: 0.373 },
  { id: 'fsr2', cat: 'post', label: 'FSR2 upscaling', minDelta: 0.358 },
  { id: 'dof_far', cat: 'post', label: 'Depth of field (far)', minDelta: 2.42 },
  { id: 'dof_near', cat: 'post', label: 'Depth of field (near)', minDelta: 0.617 },
  { id: 'auto_exposure', cat: 'post', label: 'Auto exposure', minDelta: 11.6 },
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
  { id: 'skeletal_animation', why: 'the fixture builds a two-bone skinned strip procedurally, but its skin binding does not actually deform -- a 2.2 rad pose change moves only 0.045 mean delta on native Vulkan, which is indistinguishable from skinning being broken. The GPU skinning path is a real Forward+ feature and this SHOULD be covered; the fixture needs a working skin (or a committed skinned mesh) first.' },
  { id: 'particle_trails', why: 'GPUParticles3D.trail_enabled produces no measurable change in this fixture on native Vulkan (0.000), so there is nothing to assert against. Needs a draw pass and transform_align set up for trails before it can be covered.' },
  { id: 'lightmap_gi', why: 'needs an editor-baked lightmap; cannot be produced at runtime from GDScript, so it cannot be driven by this fixture. Needs a separate fixture with committed bake data.' },
  { id: 'occlusion_culling', why: 'needs baked occluders and is a visibility optimization, not a rendering path -- a correct result is pixel-identical, so an A/B delta check cannot see it. Needs a draw-call-count assertion instead.' },
  { id: 'multiview_xr', why: 'WebGPU has no multiview; the driver reports SUPPORTS_MULTIVIEW false and the renderer takes the single-view path. Nothing to compare.' },
  { id: 'vrs', why: 'WebGPU has no variable shading rate; SUPPORTS_ATTACHMENT_VRS is false by design.' },
  { id: 'compositor_effects', why: 'CompositorEffect runs user-supplied RD code; there is no engine behavior to assert, only the user callback.' },
  { id: 'volumetric_fog_volumes', why: 'FogVolume nodes with custom fog shaders -- a distinct path from environment volumetric fog, and not yet exercised. Should be added.' },
  { id: 'stencil', why: 'BaseMaterial3D.stencil_mode -- added recently upstream and not yet exercised. Should be added.' },
  { id: 'bent_normal', why: 'BaseMaterial3D.bent_normal_enabled -- added recently upstream and not yet exercised. Should be added.' },
];

export const byId = Object.fromEntries(FEATURES.map((f) => [f.id, f]));
