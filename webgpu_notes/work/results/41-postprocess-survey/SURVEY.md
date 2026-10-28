# Survey: post-processing effects Godot lacks (TASKS.md §2.48, work task 41)

Research only: no build and no GPU run. Every "absent" below is a grep of this tree at `46bd8e96b8` (webgpu-4.8):
`servers/rendering`, `drivers/gles3`, `scene/`, `doc/classes`. Other engines are cited from their docs. Excluded, as §2.48
says: everything in the camera batch (§4.47-4.55, work tasks 40 and 44), meaning lens optics, motion blur and rolling
shutter, bokeh/blades/tilt-shift/starburst, halation, lens flare, veiling glare, diffusion filters, metering, AWB,
in-camera NR and sharpening, monitoring overlays, grain and camera LUTs.

## What Godot already has (so it is not a gap)

| area | where in this tree | renderers |
|---|---|---|
| Tonemappers: Linear, Reinhard, Filmic (fixed Hable constants), ACES (fitted), AgX (contrast, white) | `shaders/effects/tonemap.glsl:154-300`, `Environment.tonemap_*` | all |
| Brightness/contrast/saturation; 1D (per channel) or 3D color correction LUT | `Environment.adjustment_*`, `apply_color_correction()` `tonemap.glsl:512` | all (`drivers/gles3/shaders/effects/post.glsl` `USE_BCS`, `USE_COLOR_CORRECTION`) |
| Glow, glow map (works as a lens-dirt mask: Unreal "Dirt Mask", Unity Bloom "Dirt") | `Environment.glow_*` | all (reduced in Compatibility) |
| SSAO (Intel ASSAO, `shaders/effects/ssao.glsl` header), SSIL, SSR, SDFGI, volumetric fog | `Environment.ss*`, `sdfgi_*`, `volumetric_fog_*` | SSAO Forward+ and Compatibility; the rest Forward+ only (Environment.xml notes) |
| Screen-space contact shadows (upstream, 4.7/4.8) | `effects/screen_space_contact_shadows.glsl`, `rendering/lights_and_shadows/contact_shadow/*`, `Light3D.shadow_contact_shadows_*` | Forward+ only (web too, §4.29) |
| Sharpening outside FSR | camera finish pass, `CameraAttributes.sharpening_*` (§4.50) | all |
| HDR output, reference/max luminance | `Window.hdr_output_requested`, `DisplayServer.window_set_hdr_output_*` | desktop Vulkan/D3D12/Metal; **not web** (`swap_chain_get_hdr_output_supported()` returns false, `rendering_device_driver_webgpu.cpp:4254`; TASKS §2.31) |
| Refraction / heat-haze / underwater distortion by material | spatial shaders with `hint_screen_texture` | all |
| Custom full-screen passes | `CompositorEffect` (pre/post opaque, post sky, pre/post transparent) | Forward+, Mobile |
| "External" log LUT replacing the tonemapper (HDRP Tonemapping "External") | camera LUT log inputs (§4.48) | all |

## Budget facts every candidate is judged against

- **Forward+ tonemap push constant is full** (128 bytes, `effects/tone_mapper.h:126-152`); camera LUT bits live in
  `flags` 8-23. Mobile's has `pad[2]` (8 bytes) left. Compatibility uses a UBO (room).
- **Forward+ tonemap already uses all four descriptor sets** (`tonemap.glsl:47-59`: 0 source, 1 auto exposure +
  CameraEffects + AWB, 2 glow + glow map + glare, 3 color correction + camera LUT). On WebGPU group 3 also carries the
  push-constant ring (binding 120). A new tonemap input therefore goes **into an existing set** (set 3 binding 2 is the
  natural slot), never a fifth group. Mobile's tonemap is one set (bindings 0-6).
- **CameraEffects UBO** (`camera_effects_inc.glsl`, §4.52): `header.w` and `reserved[15]` free, plus `lens[7].zw`.
  It is filled from **CameraAttributes**, so only camera-owned features belong in it. Environment-owned parameters
  have no such buffer today.
- Texture budget is not a concern in post shaders (the tonemap fragment stage binds ~8 of the 48).
- **Compatibility tonemaps in the scene shader**: its post pass sees display-referred light, so nothing pre-tonemap
  (HDR grading, local exposure) can run there. **Mobile's RGB10A2 buffer tops out at 2.0**, which weakens anything
  that needs highlights (local exposure, HDR-domain grading).

## Ranked gaps

Effort is in sessions (one worker session as in the camera batch, verification included). "Web" is WebGPU Forward+
unless stated.

### 1. Color grading controls (lift/gamma/gain, shadows-midtones-highlights, channel mixer, split toning, color filter / hue shift / vibrance, curves including hue-vs curves)

- **Others:** URP has all of them as volume overrides: Lift Gamma Gain, Shadows Midtones Highlights, Channel Mixer,
  Split Toning, Color Adjustments (post exposure, contrast, color filter, hue shift, saturation), Color Curves (master,
  R, G, B, Hue vs Hue, Hue vs Sat, Sat vs Sat, Lum vs Sat), https://docs.unity3d.com/6000.0/Documentation/Manual/urp/EffectList.html
  (HDRP has the same set). Unreal: Color Grading with Global/Shadows/Midtones/Highlights × saturation/contrast/gamma/
  gain/offset, plus scene color tint and blue correction,
  https://dev.epicgames.com/documentation/en-us/unreal-engine/color-grading-and-the-filmic-tonemapper-in-unreal-engine.
  Blender compositor: Color Balance (Lift/Gamma/Gain and ASC CDL Offset/Power/Slope), Color Correction (master/
  shadows/midtones/highlights), Hue Correct, RGB Curves,
  https://docs.blender.org/manual/en/latest/compositing/types/color/adjust/color_balance.html.
- **This tree:** absent (`lift_gamma_gain`, `channel_mixer`, `split_ton`, `shadows_midtones`, `hue_vs`, `vibrance`,
  `hue_shift`, `color_filter`: 0 hits). Only BCS and a user LUT; per-channel curves are possible today only by hand-
  painting a `GradientTexture1D` as the 1D color correction.
- **Usefulness:** highest of the list. Every other engine ships these; today a Godot user must grade in an external
  tool and export a LUT, and cannot animate a grade (a hit flash, a mood shift by area).
- **How (recommended):** bake the whole grade into an internal 33³ 3D LUT whenever a grading property changes, the
  way URP and HDRP bake their grading LUT (URP Asset "Grading Mode" and "LUT Size",
  https://docs.unity3d.com/6000.0/Documentation/Manual/urp/universalrp-asset.html). The bake composes BCS, the new
  controls and the user's own color correction (1D or 3D) on the GPU (33 small draws or one compute dispatch, only on
  change), and the result is bound **in the existing color correction slot** with the existing 3D-LUT variant.
  Curves are Godot `Curve` resources sampled into the bake.
- **Per renderer:** Forward+, Mobile, Compatibility, web: all. Display-referred (after the tonemapper, where Godot's
  adjustments already sit), so identical across renderers.
- **Web cost:** zero new passes, **zero new bindings, no shader interface change** (the 3D variant and binding exist),
  one 3D fetch per pixel that the BCS math replaces; 33³ × RGBA16F = 288 KB of VRAM; the bake is ~36k texels when a
  property changes (an animated grade re-bakes every frame: still far under 0.1 ms). No push constant or CameraEffects
  involvement.
- **Option:** an HDR (pre-tonemap, log-domain) grading mode as URP's "High Dynamic Range" grading: Forward+ only, needs
  a second binding (set 3 binding 2) and changes where the tonemapper runs; worth it only after the LDR version.
- **Belongs:** Environment, a new "Color Grading" group (`grading_*`) beside Adjustments.
- **Effort:** 2 sessions (bake + 8 control groups + inspector + three renderers + tests against a CPU reference).

### 2. Local exposure / local tonemapping (and exposure fusion)

- **Others:** Unreal Local Exposure (Lens > Exposure), methods Bilateral and Fusion; highlight/shadow contrast,
  detail strength, blurred luminance blend,
  https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine. Unity and Blender: none
  built in (Blender's Tonemap node R/D Photoreceptor is global).
- **This tree:** absent (`local_exposure`, `local_tonemap`, `bilateral_grid`: 0 hits).
- **Usefulness:** high for realism: interiors with windows, caves to sky, night streets, where one global exposure
  either blows the window or crushes the room. It is the single feature that most separates a modern game image from
  Godot's.
- **How:** Unreal's Bilateral method: luminance into a bilateral grid (compute, ~(W/64)×(H/64)×32), blur the grid, a
  blurred-luminance mip (the auto exposure or glare chain can provide it), and the tonemap pass reduces base-layer
  contrast before tonemapping. Exposure fusion (Mertens 2007, Unreal's "Fusion") as a later second method.
- **Per renderer:** Forward+ (desktop and web: compute is fine on WebGPU). Mobile: skip (RGB10A2 caps at 2.0, so
  there is little range to compress). Compatibility: impossible (no pre-tonemap post).
- **Web cost:** 2-3 compute dispatches on small textures (<1 MB), **one new sampler + texture in tonemap set 1**
  (beside auto exposure), ~6 floats: fit in CameraEffects `exposure` / `reserved` region with a `header.z` bit if it
  lives on CameraAttributes. No push constant change. ~0.1-0.2 ms at 1080p estimated (Unreal's own figure is not
  published; to measure).
- **Belongs:** CameraAttributes (exposure group, as Unreal puts it under exposure, and it gets the CameraEffects UBO
  for free); alternative Environment (tonemap group), which would need its own buffer.
- **Effort:** 2-3 sessions.

### 3. Screen-space light shafts (god rays)

- **Others:** Unreal per directional light: Light Shaft Occlusion (masks fog/atmosphere) and Light Shaft Bloom (radial
  blur of the scene around the sun), 0.5 + 0.68 ms at 1080p on a GTX 680 per Epic,
  https://dev.epicgames.com/documentation/en-us/unreal-engine/using-light-shafts-in-unreal-engine. HDRP and Forward+
  get them physically from volumetric fog; URP has nothing built in.
- **This tree:** absent (`light_shaft`, `god_ray`, `sun_shaft`: 0 hits). Volumetric fog does it on Forward+ only.
- **Usefulness:** high where volumetric fog is unavailable or too expensive: Mobile, Compatibility, and web on phones
  (the Razr 60 class, §2.34), which is this fork's audience. On Forward+ it is a cheaper, more art-directable
  alternative.
- **How:** GPU Gems 3 ch. 13 (Mitchell): quarter-resolution mask (sky/bright pixels from depth), 2-3 radial blur
  passes toward the sun's screen position, additive composite.
- **Per renderer:** Forward+, Mobile, Compatibility (on its display-referred color, thresholded near white), web.
  Mobile needs the depth copy it already makes for `hint_depth_texture`.
- **Web cost:** 3-4 small raster passes at quarter resolution (~0.5 MB each at 1080p), composite as an additive blend
  pass into the HDR color before tonemapping: **no new tonemap binding**, no push constant or CameraEffects change.
- **Belongs:** Environment (`light_shafts_*`: intensity, threshold, decay, tint) with a `DirectionalLight3D` opt-in
  flag (Unreal's per-light choice), first opted-in light wins.
- **Effort:** 1.5-2 sessions.

### 4. Custom tonemap curve (toe / shoulder / slope / clip)

- **Others:** HDRP Tonemapping "Custom": toe strength/length, shoulder strength/length/angle, gamma,
  https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Post-Processing-Tonemapping.html.
  Unreal Film: slope, toe, shoulder, black clip, white clip (post-process effects page,
  https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-effects-in-unreal-engine). Blender:
  view transform Looks (contrast presets).
- **This tree:** absent. Filmic's constants are fixed (`tonemap.glsl:159-165`); only AgX has contrast/white.
- **Usefulness:** medium. Lets an art director match a reference curve without a LUT round trip; most users are served
  by AgX + grading.
- **How:** Hable's piecewise power curve (2017) with Unreal-like parameters, evaluated from ~12 precomputed floats,
  or simpler and general: a 256-entry log2-domain 1D LUT baked on change.
- **Per renderer:** all three, web.
- **Web cost:** no pass. The Forward+ push constant is full, so it needs **one binding in set 3** (binding 2: a 1D
  LUT or a small Environment-owned UBO); Mobile set 0 binding 7; Compatibility a UBO field. It is the first
  Environment-owned tonemap parameter that does not fit the push constant: whichever buffer it adds should be the place
  later Environment post parameters go (e.g. an HDR grading mode from item 1).
- **Belongs:** Environment, `TONE_MAPPER_CUSTOM` + `tonemap_custom_*`.
- **Effort:** 1-1.5 sessions.

### 5. Panini projection

- **Others:** URP and HDRP Panini Projection override (distance, crop to fit),
  https://docs.unity3d.com/Manual/urp/Post-Processing-Panini-Projection.html. Unreal `r.LensDistortion.Panini.D/.S`,
  https://dev.epicgames.com/documentation/unreal-engine/panini-projection-in-unreal-engine.
- **This tree:** absent (`panini`: 0 hits; `Camera3D.projection` is perspective/orthogonal/frustum).
- **Usefulness:** medium, high for one genre: wide-FOV first-person games (100°+), where the edges stretch.
- **How:** a resample, the same kind the lens pass already does at its first read (`camera_lens_inc.glsl`, §4.52):
  add the Panini mapping to that resample, with auto crop.
- **Per renderer:** wherever the lens resample runs: Forward+, Mobile, Compatibility, web.
- **Web cost:** zero passes, zero bindings; D and S fit **`lens[7].zw`** of the CameraEffects UBO plus a `header.x`
  bit; Compatibility forces its post pass while on (as lens effects already do). Caveat shared with distortion: screen
  ↔ world mapping (`Camera3D.unproject_position`, picking, HUD markers) does not know about it unless Camera3D gets the
  same mapping; Unreal documents the same mismatch. Building that in is part of the effort.
- **Belongs:** CameraAttributes (reuses the lens plumbing), with Camera3D's project/unproject made aware.
- **Effort:** 1 session (+0.5 for the Camera3D mapping).

### 6. GTAO-quality ambient occlusion (and visibility bitmask AO+IL)

- **Others:** Unreal GTAO (`r.AmbientOcclusion.Method 1`, since 4.24); Blender Eevee's "fast GI" screen-space horizon scan for AO and GI (Raytracing panel, Fast GI Approximation),
  https://docs.blender.org/manual/en/latest/render/eevee/render_settings/raytracing.html; HDRP's SSAO override
  (https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Override-Ambient-Occlusion.html;
  the page does not name its algorithm).
- **This tree:** Godot's SSAO is Intel ASSAO (2016, `ssao.glsl` header); `gtao`, `hbao`: 0 hits.
- **Usefulness:** medium. GTAO (Jimenez 2016, XeGTAO) is closer to ground truth, gives bent normals and a multi-bounce
  term, and its noise resolves well under TAA. The visible gain over ASSAO "Ultra" is moderate.
- **Overlap:** TASKS §2.34 Tier A RT AO writes the same `ao_buffer`. GTAO would be the screen-space middle tier
  between ASSAO and RT AO; decide after the RT AO decision.
- **Per renderer:** Forward+ and web (compute). Compatibility keeps its own SSAO; Mobile has none.
- **Web cost:** 2-3 compute passes (main at half or full resolution, denoise ×2), same output texture: no scene
  shader or interface change. Comparable to ASSAO High.
- **Belongs:** project setting `rendering/environment/ssao/method` (ASSAO | GTAO), Environment SSAO properties reused.
- **Effort:** 2-3 sessions (visibility bitmask replacing SSIL as well: +2).

### 7. HDR output on web

- **Others:** all three ship HDR output on desktop; this fork has it on desktop too (upstream).
- **This tree:** WebGPU reports no HDR swap chain (`rendering_device_driver_webgpu.cpp:4254`), TASKS §2.31.
- **How:** `GPUCanvasConfiguration.toneMapping = {mode: "extended"}` with an `rgba16float` canvas (shipped in Chrome;
  Firefox/Safari support to check), `matchMedia("(dynamic-range: high)")` for support; the browser exposes no peak
  luminance, so reference/max luminance stay user settings.
- **Usefulness:** low-medium (HDR monitors + Chrome). It also unblocks HDR-domain grading being worth anything on web.
- **Web cost:** twice the swap-chain bandwidth (8 vs 4 bytes per pixel) while on; no shader change (the HDR path
  exists).
- **Belongs:** the web platform + WebGPU context driver, behind the existing `Window.hdr_output_requested`.
- **Effort:** 1-1.5 sessions (needs an HDR display to verify).

### 8. Color vision deficiency simulation and correction (accessibility)

- **Others:** Unreal `SetColorVisionDeficiencyType` (deuteranope/protanope/tritanope, severity, correct, show
  correction), https://docs.unrealengine.com/4.26/en-US/BlueprintAPI/Widget/Accessibility/SetColorVisionDeficiencyType/.
  Unity and Blender: none built in.
- **This tree:** absent (`deficiency`, `daltoni`: 0 hits; `deficien` only in AudioEffectEQ.xml).
- **Usefulness:** medium: an accessibility option players ask for, and a QA tool to simulate it during development.
- **How:** Machado 2009 matrices (simulation) and daltonization (correction), applied to the whole window,
  UI included, at the final blit.
- **Per renderer:** RD blit (`shaders/blit.glsl`, push constant has `pad1/pad2`: pack type + severity in one uint)
  for Forward+/Mobile/web; Compatibility's final copy needs its own uniform.
- **Web cost:** a 3x3 matrix per output pixel in a pass that already runs: nothing measurable.
- **Belongs:** project settings (`display/accessibility/color_vision_*`) + a runtime `Window`/`DisplayServer` setter.
- **Effort:** 0.5-1 session.

### 9. Micro shadows

- **Others:** HDRP Micro Shadows (shadows from normal and AO maps for the directional light, opacity),
  https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Override-Micro-Shadows.html.
- **This tree:** absent (`micro_shadow`: 0 hits).
- **Usefulness:** low-medium; cheap realism on detailed materials under sun light.
- **How/cost:** a few ALU in the directional light term of the scene shaders (all renderers, web), one flag + one float
  in the scene data UBO (no new binding). Not a post pass: it lives in the light loop.
- **Belongs:** Environment (`micro_shadows_*`), or a BaseMaterial3D flag.
- **Effort:** 0.5-1 session.

## Checked and not recommended

| candidate | finding | why not |
|---|---|---|
| Contact shadows on Mobile / Compatibility | Forward+ has them (above) | they need a depth prepass before opaque lighting; Mobile and Compatibility have none, adding one costs more than the effect |
| Screen-space distortion buffer (HDRP Distortion) | per-material refraction with `hint_screen_texture` already works in all renderers | only adds stacking of overlapping distorters; niche |
| Outlines / edge detection, Kuwahara, posterize, pixelate | none of Unreal/Unity ship them as built-in volumes; Blender's compositor does (Filter, Kuwahara, Pixelate) | stylistic; `CompositorEffect` or a screen shader already does them; at most a sample effect in the docs |
| Sharpening outside FSR | exists (camera finish, §4.50) | excluded/covered |
| Lens dirt | `Environment.glow_map` does it | covered |
| FFT / convolution bloom (Unreal) | glare chain, starburst, diffusion (§4.54) | overlaps the camera batch |
| Underwater | only HDRP's water system does it | fog volumes + materials; not a post effect |

## Where they go, at a glance

| # | effect | belongs | Forward+ | Mobile | Compat | web | new passes | new bindings | push constant / CameraEffects | effort |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | color grading (LDR, baked LUT) | Environment | yes | yes | yes | yes | bake on change | 0 | none | 2 |
| 2 | local exposure | CameraAttributes | yes | no | no | yes | 2-3 compute | 1 (set 1) | CameraEffects `exposure`/`reserved` | 2-3 |
| 3 | light shafts | Environment + DirectionalLight3D | yes | yes | yes | yes | 3-4 quarter-res | 0 in tonemap | none | 1.5-2 |
| 4 | custom tonemap curve | Environment | yes | yes | yes | yes | 0 | 1 (set 3 b2) | push constant full → buffer | 1-1.5 |
| 5 | Panini projection | CameraAttributes (+Camera3D mapping) | yes | yes | yes | yes | 0 | 0 | `lens[7].zw` + `header.x` bit | 1-1.5 |
| 6 | GTAO | project setting | yes | no | no | yes | 2-3 compute (replacing ASSAO's) | 0 | none | 2-3 |
| 7 | web HDR output | platform | n/a | n/a | n/a | yes | 0 | 0 | none | 1-1.5 |
| 8 | color vision deficiency | project settings + Window | yes | yes | yes* | yes | 0 | 0 | blit `pad` | 0.5-1 |
| 9 | micro shadows | Environment | yes | yes | yes | yes | 0 | 0 | scene UBO flag | 0.5-1 |

\* Compatibility through its own final copy.

**Recommendation:** build 1 (grading), 3 (light shafts) and 5 (Panini) now: all renderers, web-cheap, no interface
risk. Then 2 (local exposure), the biggest realism gain but Forward+ only. 8 is cheap and worth doing alongside. Defer
4 (until a second Environment parameter needs the same buffer), 6 (after the RT AO decision), 7 (when an HDR display is
available to verify), 9 (low value).
