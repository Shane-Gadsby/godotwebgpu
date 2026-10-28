# For the wiki: GTAO ambient occlusion (Engine Additions)

Suggested page: `Engine-Additions-GTAO.md` (or a section of a rendering page), linked from Engine Additions.

---

GTAO (Ground Truth Ambient Occlusion) is a second algorithm for the screen-space ambient occlusion you already turn on with `Environment.ssao_enabled`. It follows the scene's geometry more closely than Godot's default ASSAO: creases, contact shadows under objects and the darkening where a wall meets the floor come out closer to a ray traced result, and it keeps open flat surfaces clean. It is the method from Jimenez et al., "Practical Realtime Strategies for Accurate Indirect Occlusion" (2016), ported from Intel's XeGTAO.

It works in the Forward+ renderer, on desktop and in web exports (WebGPU). The Mobile renderer has no screen-space ambient occlusion, and Compatibility always uses its own.

## Turning it on

Project Settings > Rendering > Environment > SSAO > **Method**: `ASSAO` (default) or `GTAO`. The setting applies to the whole project; every `Environment` with SSAO enabled then uses GTAO. Nothing else changes: the scene reads the same ambient occlusion texture, so the light affect and AO channel settings, and the SSAO debug draw (Viewport > Display Advanced > SSAO), work as before.

From code, the same switch is `RenderingServer.environment_set_ssao_method()`:

```gdscript
RenderingServer.environment_set_ssao_method(RenderingServer.ENV_SSAO_METHOD_GTAO)
```

## Settings

GTAO reads the settings you already have:

| Setting | With GTAO |
|---|---|
| `Environment.ssao_radius` | The world-space distance occluders count from, as with ASSAO. |
| `Environment.ssao_intensity` | Scales the occlusion relative to the default `2.0`. At `2.0` you get GTAO's reference result. |
| `Environment.ssao_power` | Scales GTAO's contrast curve relative to the default `1.5`. At `1.5` you get the reference result. |
| `Environment.ssao_sharpness` | How strongly the denoiser keeps occlusion from bleeding across depth edges. |
| `Environment.ssao_detail`, `ssao_horizon` | ASSAO only; ignored. |
| `Environment.ssao_light_affect`, `ssao_ao_channel_affect` | Unchanged (they act where the scene uses the AO). |
| Project setting `ssao/quality` | Directions (slices) and steps per direction: Very Low 2×2, Low 3×2, Medium 3×3, High 9×3, Ultra 9 to 18 × 4 following `ssao/adaptive_target`. |
| Project setting `ssao/half_size` | Computes GTAO at half resolution in each dimension (ASSAO's half size is a quarter), then upsamples with depth awareness. |
| Project setting `ssao/blur_passes` | Number of denoise passes (edge-aware 3×3). `0` disables denoising. |
| Project settings `ssao/fadeout_from`, `fadeout_to` | Fade the occlusion out with distance, as for ASSAO. |

So a project that only switches the method to GTAO keeps its look settings and gets the reference result at the default intensity and power.

## Quality and cost

Measured on a test scene against a ray traced reference (cosine-weighted visibility within the AO radius), GTAO at Medium is about 25% closer per pixel than ASSAO at any quality, and its occlusion where geometry meets (creases, contact) is much closer: ASSAO under-darkens it by about 0.15, GTAO Medium by 0.05.

Cost at 1920×1080 on an RTX 4080 SUPER (desktop Vulkan), for the AO passes alone: GTAO Medium at half size about 0.3 ms, the same as ASSAO Medium (the default); GTAO Medium at full size about 0.5 ms; High (9 directions) about 0.55 ms at half size and 2 ms at full size. Medium at half size is the recommended setting.

Very Low and Low use fewer directions per pixel and are visibly noisier without TAA. With TAA on (or FSR 2), GTAO changes its noise pattern every frame so TAA averages it out; without TAA the pattern stays fixed, so a still image does not shimmer.

## Example

```gdscript
# project.godot: rendering/environment/ssao/method=1 (GTAO), or at run time:
RenderingServer.environment_set_ssao_method(RenderingServer.ENV_SSAO_METHOD_GTAO)

var env := Environment.new()
env.ssao_enabled = true
env.ssao_radius = 1.0  # meters
$WorldEnvironment.environment = env
```

## Limits

- Forward+ only (desktop and web). Mobile: no SSAO. Compatibility: always its own SSAO.
- Screen space: like any screen-space AO it cannot see geometry hidden behind other objects or outside the screen, so occlusion fades near the screen edges and behind silhouettes.
- No multi-bounce (colored) term and no bent normals: GTAO here writes the same single-channel AO texture as ASSAO.
