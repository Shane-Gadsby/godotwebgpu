# Task 46: screen-space light shafts (god rays)

Branch `work/46` on webgpu-4.8 at 7b978e6baa. Worker-46, 2026-10-28.

## What changed

**API** (all default off):
- `DirectionalLight3D.light_shafts_enabled` (RS `light_directional_set_light_shafts`): the opt-in. The first directional
  light that opts in is the sun (Unreal's per-light choice, SURVEY entry 3).
- `Environment.light_shafts_enabled/_intensity (1.0)/_threshold (1.0)/_decay (2.0)/_tint (white)`, group "Light Shafts"
  (RS `environment_set_light_shafts`).
- `ProjectSettings.rendering/environment/light_shafts/quality` Low/Medium/High = 16/32/64 samples (RS
  `environment_set_light_shafts_quality`, enum `EnvironmentLightShaftsQuality`), read at renderer init as DOF's.
- Class reference for all of it (doctool clean; its unrelated CameraAttributes.xml reorder was reverted).

**Model** (`RendererEnvironmentStorage`, `servers/rendering/storage/environment_storage.cpp`, comment block "Light
shafts"): Mitchell's radial blur (GPU Gems 3 ch. 13) as a weighted mean over screen distance in frame heights:
`S(p) = ∫₀ᴸ e^(-decay t) M(p + t u) dt / ∫₀ᴸ e^(-decay t) dt`, M = sky (depth 0, reverse Z) above the threshold
(luminance soft knee, hue kept), u toward the sun, L = min(distance to the sun, ln(1000)/decay). Independent of
resolution and sample count. The shader takes N stratified samples, each of the mask prefiltered along the same line
over one stride (ln(1000)/decay/N, 16 taps), so small bright suns are integrated instead of hit-or-missed per pixel;
this smooths only lengthwise. Off-frame mask samples clamp to the frame edge (no pop as the sun crosses it); the sun's
fade is 1 - smoothstep over 0.5 frame heights outside the frame (look choice) and 0 behind the camera
(`light_shafts_compute_sun()`, shared by both renderer families; orthogonal cameras get parallel shafts).
Two earlier versions were measured and replaced (commit messages say why): a tail term at the sun that made a small
bright sun bloom through every occluder, and exponential importance sampling without the prefilter, which showed a
coarse diagonal noise pattern on bright suns.

**RD (Forward+, Mobile, web)**: `RendererRD::LightShafts` (`renderer_rd/effects/light_shafts.*`,
`shaders/effects/light_shafts.glsl`): 4 raster passes — mask, prefilter, march at a quarter of width and height
(RGBA16F), composite as an additive blend into the frame's color — run first in
`_render_buffers_post_process_and_tonemap()`, before DOF, motion blur, auto exposure, glow and tonemapping. Own push
constant (64 bytes); no tonemap binding, no CameraEffects change. Depth: the depth texture, or with MSAA the R32F
resolve (`MODE_MASK_RESOLVED_DEPTH`, the name-based depth classification, as camera motion blur). Mobile skips its
post-process subpass while shafts are enabled and, with MSAA, resolves depth as for DOF. Created on first use.
**Compatibility**: `GLES3::LightShafts` (mask + prefilter + march, `drivers/gles3/shaders/effects/light_shafts.glsl`)
and a `USE_LIGHT_SHAFTS` specialization of `post.glsl` (texunit 6) that adds them in linear light after SSAO, before
vignetting and the tone curve; enabling forces the post pass and, like glow, the 10-bit range-4 frame. Single view
only. `wgsl_precompile.py` lists the 5 variants.

## Verification

Native: xvfb, RTX 4080 SUPER, Vulkan, 1280x720 unless said, Linear tonemapper, sRGB-decoded 8-bit. Scripts in
`tools/`; the reference is an independent numpy implementation of the model (`tools/analyze.py`) that mirrors the
quarter-resolution box mask, the prefilter and the bilinear reads. Shafts = linear(on) - linear(off).

- **Defaults bit-identical**: base editor (7b978e6baa, built in a detached worktree) vs work/46, shafts off, two scenes
  (lit bars + shadows + glow + Filmic; sky + 4x MSAA) x Forward+ / Mobile / Compatibility: **max diff 0, 0 px, 6/6**.
- **Enabled but no light opted in**: Forward+ 0 px; Mobile max 1/255 on 5,226 px (post-process subpass skipped);
  Compatibility max 2/255 on 86% of px (post pass and 10-bit frame forced, as glow does). Sun behind the camera: 0 px
  in Forward+, Mobile and WebGPU; Compatibility the same 1/255 path difference.
- **Model vs render** (correlation / relative RMS over 8x8 blocks where the model > 0.01):
  - fence of bars across a sky halo (occluded shafts, direction toward the sun): Forward+ **0.998 / 7.2%**, Mobile
    0.997 / 8.0%, Compatibility 0.999 / 9.8%, **WebGPU 0.990 / 12.3%**; 4x MSAA (resolved-depth variant) Forward+
    0.998 / 7.6%, Mobile 0.997 / 8.4%.
  - sun disk on a plain sky, decay 2, quality Low/Medium/High: Forward+ 0.999 / 7.1, 6.1, 6.1%; Mobile 0.999 / 3.9,
    3.4, 3.3%; Compatibility 0.999 / 10.7, 9.6, 9.5% (its sky shader renders darker than RD's, so M is read from the
    render); WebGPU decay 2 / 4: **0.9995 / 4.9%, 0.9996 / 4.4%**.
  - resolution: 640x360 / 1280x720 / 1920x1080 all correlate >= 0.9988 (shafts sized in frame heights).
  Mean level within 2-11% of the model (Forward+ ~8% low, Mobile 3% high, Compatibility 11% low).
- **Noise** (residual RMS around ring means, linear; one 8-bit step there is 0.0041): Forward+ Low/Med/High 0.00126 /
  0.00113 / 0.00107 (before the prefilter 0.0051 / 0.0024 / 0.0015, with a visible pattern on bright suns), Mobile
  0.00122 / 0.00109 / 0.00105, Compatibility 0.00077 / 0.00067 / 0.00063. With an extreme sun (radiance 30) a faint
  pattern remains far from the sun.
- **Frame edges** (sky halo, sun moved right; total shaft energy): 39.7k -> 39.1k -> 37.9k -> 33.5k as the sun crosses
  the edge (continuous; before the clamp fix it fell 15x in one step), then 14.1k, 2.8k and 0 by 0.24 frame heights
  out (the visible sky stops exceeding the threshold before the 0.5 fade does). Mobile the same within 3%;
  Compatibility proportionally, with less sky above threshold.
- **WebGPU vs native Forward+** (system Chrome headed, Vulkan, adapter asserted nvidia/lovelace, not fallback, 48
  textures; 0 console errors in 11 cases): shaft energy web/native 0.995 (bars), 0.988 (disk), 1.028 (lit scene with
  glow); mean |shaft difference| 0.0010 / 0.0006 / 0.0071 linear. `images/look.jpg`, `images/bars_model.jpg`.
- **Cost**: WebGPU per pass (`gpu_time.mjs --passes`, 1152x648 canvas, Medium; `webgpu_passes.txt`): mask 0.024,
  prefilter 0.021, composite 0.022 ms, march < 0.015 ms; GPU busy 0.33 -> 0.40 ms/frame (+0.07 ms; ~+0.2 ms at 1080p
  by pixel count). Native `viewport_get_measured_render_time_gpu` at 1920x1080 was too erratic to quote precisely:
  Forward+ +0.1-0.5 ms, Mobile +0.1-0.5 ms, Compatibility +2-4 ms on top of ~2-3 ms for the forced post pass (GL timer
  readings here are erratic; worth a re-measure on the phone).
- Builds: editor `dev_mode=yes` (werror) clean; web `template_debug` nothreads non-dlink clean.
- `webgpu_notes/work/smoke.sh`: **SMOKE OK** (302 modules, 0 invalid; `smoke_tail.txt`).

## Interface references (regenerated on this branch)

Only additions: 10 light_shafts modules in `interface_reference.json` (mask, mask_resolved_depth, prefilter, blur,
composite x vert/frag; source string appended) and their 20 lines in `scan_reference.txt`. Nothing else changed.

## What the lead must still run

- Merge conflicts are likely next to task 45's blocks (Environment properties after diffusion filter, `post.glsl`
  specializations and texunits — mine is texunit 6, `USE_LIGHT_SHAFTS` appended last —, RS bindings,
  `rendering_server_enums.h`, `wgsl_precompile.py`). Regenerate the references after merging.
- Not run: Forward+ matrix / smoketest (no fixture uses the feature; defaults bit-identical), Firefox, the phone.

## Risks

- Compatibility: enabling light shafts changes its frame by up to 2/255 even with no light opted in (forced post pass
  and 10-bit frame), and its sky shaders render darker than RD's, so thresholds tuned on Forward+ show fewer shafts.
- Screen-space limits as documented: only visible sky scatters; very bright small suns leave faint noise far out.
