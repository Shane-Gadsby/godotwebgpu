# Task 72: the camera's frame without the monitoring overlays: SUMMARY

## What changed

**Camera3D** (`scene/3d/camera_3d.*`, Monitoring group):
- `monitoring_clean_texture_enabled` (bool, default `false`, inspector: Monitoring, after Visible in Output) and
  `get_monitoring_clean_texture() -> Texture2D` (`null` while disabled; the same texture while enabled).
- While enabled, every frame the camera renders is also copied into that texture **before the overlays** (zebras, false
  color, focus peaking, drawn histogram) and before the viewport's 2D, after tonemapping and NR/sharpening: what the
  viewport texture shows with the overlays off. Independent of Visible in Output.
- Disabling frees everything; a script that kept the texture then reads `null` from it (documented).
- The texture is an internal class `CameraMonitoringCleanTexture` (`GDREGISTER_INTERNAL_CLASS`, no class XML): a
  Texture2D owning a server texture (`texture_2d_placeholder_create()` on enable, so the RID exists at once), overriding
  `get_rid()` / `get_image()` / `get_image_async()`; size = the camera's viewport texture size.
- Enable + read + disable in one function works (`texture_2d_get_async()` reads after the frame's draw). Docs show the
  callback form and task 71's `get_image_awaitable()` (as `[code]` text: 71 is not on work/72).

**RenderingServer**: `camera_set_monitoring_clean_texture(camera, texture)` (queued). `CameraMonitoring::clean_texture`
(part of `is_active()`); unsetting it calls the new `RendererSceneRender::camera_monitoring_clean_texture_free(camera)`
(default no-op).

**RD `CameraFinish`** (Forward+, Mobile, WebGPU): per-camera `CameraCleanFrame` keyed by camera RID (like task 62's
histograms): an RD texture in the render target's format/size, recreated on resize / HDR toggle / another viewport. One
`texture_copy` per frame: from the scratch texture the overlay pass reads (overlays are always the last pass), or from
the render target when there are no overlays (no scratch then). The scratch textures gained `CAN_COPY_FROM` (no effect
on output: bit check below). `TextureStorage::render_target_copy_texture_set()` points the server texture at the copy
with the render target texture's own views/formats, so its readback gives the same Image as `Viewport.get_texture()`.

**Compatibility** (`GLES3::CameraFinish`): same design with a GL texture + FBO and `glBlitFramebuffer`, and
`GLES3::TextureStorage::render_target_copy_texture_set()` (native-handle style, not freed with the server texture).
Compatibility *does* draw zebras/peaking/false color (only the histogram is missing), so the clean frame is real there
too, not "the frame itself" as the task text guessed. `RasterizerSceneGLES3` now overrides `camera_monitoring_free()`.

No shader changed: no interface reference changes, smoke.sh not run (lead instruction).

## Verification (actual numbers; scripts in `tools/`, output in `runs/`)

`tools/project/main.gd`: SubViewports with own worlds (sky, shadowed sun, lit objects, a test chart); each check reads
the clean texture and the viewport texture with `get_image_async()` and compares bytes.

| Check | Forward+ | Mobile | Compatibility | WebGPU Chrome debug / release |
|---|---|---|---|---|
| clean (overlays on) vs viewport with overlays off: all / zebras / false color / histogram | 0 px each | 0 px each | 0 px each | 0 px each |
| same with NR + sharpening | 0 px | 0 px | 0 px | 0 px |
| overlay frame vs off (overlays really drawn) | 12.8k-230k px | same | 104k-230k (histogram 0: not drawn there) | same as native |
| one-function pattern (enable, read, disable in callback) | 0 px, 2 frames | 0 px, 2-3 frames | 0 px, 1 frame | 0 px, 5 / 2 frames |
| texture kept after disable reads null | yes | yes | yes | yes |
| two SubViewports (320x180, 400x240), own cameras | 0 / 0 px | 0 / 0 | 0 / 0 | 0 / 0 |
| one resized to 512x288 while enabled | 0 px, 512x288 | same | same | same |
| texture memory, enabled - disabled (1080p) | +8,847,360 (8,294,400 + alignment) | same | +8,294,400 | +8,294,336 |
| texture memory after disable = before | yes | yes | yes | WebGPU counter is negative (pre-existing); not checked |
| console errors / warnings | 0 | 0 | 0 | 0 / 0 |

Chrome per the launch rule; adapter asserted nvidia / lovelace / not fallback / 48 for both web runs.

- **Defaults bit-identical** vs the pre-change editor (built from 1ff3d995b5 in this worktree; `tools/bitcheck.sh`,
  960x540 lit scene): 0 px in Forward+, Mobile, Compatibility for defaults, drawn histogram with Visible in Output, and
  noise reduction. Also 0 px for the **output with the clean texture enabled** (drawn histogram + clean vs pre "drawn";
  defaults + clean vs pre "plain"): enabling it never changes what is shown.
- **Cost:** disabled = the old code path (one `clean_texture.is_valid()` check), nothing allocated; confirmed by the bit
  check and the memory numbers. Enabled = one viewport-sized texture + one copy per frame. GPU time per viewport
  (`viewport_get_measured_render_time_gpu`, 1080p, 3 alternating rounds) was dominated by clock ramp on this desktop
  (Forward+ off 0.16/1.48/1.56 ms, clean 0.63/1.82/1.83; Mobile off 0.08/0.67/0.90, clean 0.30/0.95/1.00); an 8 MB
  copy is ~0.02 ms of bandwidth. Not a number to quote. No GPU timestamps on WebGPU here.
- **First run caught a bug:** the RD scratch textures lacked `CAN_COPY_FROM`, so the copy failed with an RD error and
  the texture stayed stale (230,400 px off). Fixed; re-run above is clean with 0 errors.
- Builds: editor plain and `dev_mode=yes` (werror), web `template_debug` and `template_release` (dlink no, threads no,
  raycast): clean. `pre-commit` clean. `--doctool .` touched only Camera3D.xml and RenderingServer.xml.

## Skipped (lead instruction: only this task's checks)

Forward+ matrix, scene smoketest, perf_guard, smoke.sh (no shader interface changed), Firefox (not in Verification),
XR (documented unsupported, as the overlays).

## What the lead must still run

- After merging task 71: turn `[code]Texture2D.get_image_awaitable()[/code]` in `Camera3D.xml` into a `[method]` link,
  and check `get_image_awaitable()` on the clean texture (should work if 71 builds on the virtual `get_image_async()`,
  which the internal class overrides).
- By eye in the editor: the check box under Monitoring.

## Risks

- A camera rendered into two viewports in one frame (editor preview + game) recreates its copy every render and keeps
  the last; documented like the histogram.
- Until the camera has rendered once, the texture is the 4x4 placeholder (documented).
- `CameraMonitoringCleanTexture` is internal: `get_class()` shows it but it has no docs page.
