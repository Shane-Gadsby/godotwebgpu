# For the shared docs (work task 27)

## webgpu_tests/README.md: a new tier, beside tests/editor_drop and tests/default_folders

```bash
# Shader-bake isolation: an interactive Web export from a windowed editor (Xvfb, Vulkan) whose 3D viewport runs
# FSR 2 + TAA; fails on any "Mismatch uniform type" in the editor's own log, on an editor that drew no frame during
# the bake (inconclusive), or on an FSR 2 pass baked in the editor's flavor. ~1 min, no browser, no lease:
webgpu_tests/bake_isolation/run.sh
```

## CLAUDE.md, Build Commands, the export-time bake paragraph (one sentence to add)

While a WebGPU bake runs, `RenderingDevice::has_feature()` answers as the target only inside the baker's own
callbacks (`RD::ShaderBakeFeatureOverrideScope`, per thread); the editor's own frames in between keep its device's
answers. A shader whose variants depend on a capability must make them **groups with bake requirements** (fog.cpp,
fsr2.cpp), or refresh their text in a `ShaderRD::add_general_defines_refresh_callback()` (fsr.cpp), never build a
has_feature()-dependent variant list once at construction: the baker then bakes the editor's flavor under a cache
key the target never asks for.

## drivers/webgpu/README.md or webgpu_site/TECHNICAL_REFERENCE.md (shader baker section), if it lists the override

The capability override is scoped: `shader_bake_feature_override_set()` installs the map for the export, and
`has_feature()` consults it only on a thread inside a `ShaderBakeFeatureOverrideScope`
(`_begin_customize_resources`, `_customize_resource`, `_customize_scene`).
`shader_bake_feature_override_is_active()` still means "a bake for an overriding target is running", in or out of a
scope.
