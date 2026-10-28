# For TASKS.md (task 46)

## §4 entry: Screen-space light shafts `DONE 2026-10-28` (work task 46, SURVEY entry 3)

`DirectionalLight3D.light_shafts_enabled` opts a light in; `Environment.light_shafts_*` (intensity, threshold, decay,
tint) and `rendering/environment/light_shafts/quality` (16/32/64 samples). Mitchell's radial blur (GPU Gems 3 ch. 13)
as an exponentially weighted mean of the sky mask on the way to the sun, in frame heights, with a lengthwise prefilter
over one march stride: same result at every resolution and sample count. Four quarter-resolution raster passes in RD
(`RendererRD::LightShafts`, before DOF and tonemapping, additive composite, own push constant, no tonemap binding);
Compatibility: `GLES3::LightShafts` + `post.glsl` `USE_LIGHT_SHAFTS` (texunit 6), single view. Defaults bit-identical
(6/6 scenes x renderers vs the base editor). Render vs an independent model: correlation 0.997-0.999 native, 0.990-0.9996
WebGPU; WebGPU shafts within 1-3% of native Forward+. WebGPU cost +0.07 ms at 1152x648 (~0.2 ms at 1080p). Results:
`webgpu_notes/work/results/46-pp-light-shafts/`.

## §3 trap candidates

- **RD's luminance multiplier means scene = stored x multiplier** (Mobile stores half the light,
  `RenderSceneBuffersRD::get_luminance_multiplier()` returns 2); the Compatibility renderer's means the opposite
  (stored = encoded x multiplier, post.glsl divides). An effect that reads the frame must multiply in RD and divide in
  GLES3; getting it backwards made Mobile's light shafts silently zero.
- **The Compatibility renderer is reverse-Z too** (`GL_GEQUAL`, sky at depth 0), not GL's depth-1 far plane.
- **GLES3 shader builder parses `uniform` lines up to the end of the line**: a trailing `// comment, with, commas`
  after a uniform declaration turns into bogus enum entries (`SCALE.,`) in the generated header. Put comments above.
- **A radial blur's "sun" term must not extend past the sun**: continuing the ray at the sun's own brightness (so the
  weights sum to 1) made a small bright sun bloom over the whole frame through every occluder.

## Questions for the user

None needed to finish. Possible follow-up (default: no): an occlusion-only mode as Unreal's "Light Shaft Occlusion",
darkening fog/atmosphere instead of adding light.
