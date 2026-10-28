# For TASKS.md (§4 entry) and README

## §4.x Micro shadows (task 49, SURVEY entry 9)

`Environment.micro_shadows_enabled` (false) / `micro_shadows_opacity` (1.0), RS `environment_set_micro_shadows(env,
enable, opacity)`: directional lights shadowed by the material's AO and normal maps, HDRP style. Formula
(Brinck & Maximov, "The Technical Art of Uncharted 4", SIGGRAPH 2016, = HDRP `ComputeMicroShadowing()`):
`mix(1, saturate(N.L + 2 AO^2 - 1), opacity)`, skipped where N.L < 0, multiplied into the light's shadow factor.

- Forward+ / Mobile: `micro_shadow()` in `scene_forward_lights_inc.glsl`; applied in the per-pixel directional loop
  after `blur_shadow()`. Scene UBO: `SceneData.pad` (after `reflection_color`) is now `float micro_shadow_opacity`
  (0 = off), filled in `RenderSceneDataRD::update_ubo()`; layout and size unchanged, so no interface/scan reference
  change and no new variants (uniform branch).
- Compatibility: its own copy of `micro_shadow()` in `scene.glsl`; the UBO grew by one vec4 (`micro_shadow_opacity` + 3
  pad) in both `SceneData` structs and `RasterizerSceneGLES3::SceneState::UBO`; applied to the base pass's unshadowed
  directional lights and the additive pass's shadowed one.
- AO input = material AO (texture, shader `AO`, decals) before SSAO and before `ao_light_affect`, as HDRP's forward path.
- Not applied with per-vertex lighting (documented).
- Forward+ matrix: new arm `micro_shadows` (cat lighting; probe sphere with the bumpy normal map + a matching AO
  texture in both frames, only the Environment toggle differs). Matrix count 97 -> 98 (CLAUDE.md, README).

## Questions for the user

None. (Default choices made: Environment location, as the survey and HDRP; opacity default 1.0, HDRP's default.)
