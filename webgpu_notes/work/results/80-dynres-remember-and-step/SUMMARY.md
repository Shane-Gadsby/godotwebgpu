# Task 80: dynamic resolution remembers per window mode; `scaling_3d_dynamic_step` (SUMMARY)

Branch `work/80`, on `webgpu-4.8` at `f3c673deb2`. This task implements the user's answers on task 75:
- `dynres-remember-key`: "Also per window mode".
- `dynres-step-size`: "Expose it".

## What changed

**Remember per window mode** (`scene/main/viewport.{h,cpp}`):
- **The key.** `user://scaling_3d_dynamic.cfg` keeps the section `<rendering method> <GPU name>`, as before. The key
  is now `<viewport path>@<mode>`, for example `/root@windowed` or `/root@fullscreen`.
- **The mode** comes from the window the viewport is shown in:
  - a SubViewport uses its window's mode;
  - an embedded Window uses its host's mode;
  - the values are `windowed` (which also covers maximized and minimized), `fullscreen` and `exclusive_fullscreen`.
- **Old files.** A task 75 key (the path alone) is read as a fallback:
  - the first mode that loads takes its value;
  - it is rewritten as that mode's key, and the old key is erased, so it seeds only one mode;
  - a save also erases a leftover old key.
- **Mode changes while running.** The controller polls the mode every frame, but asks the display server only when
  the window's size changed or every 30 polls (on X11 the question is a round trip).
  - On a change, the remembered scale for the new mode is loaded. That restarts the controller from it.
  - If the new mode has no saved scale, the current scale is kept, and the next `STABLE` scale is saved under the new
    mode.
  - This is the main path on the web: a page can only go fullscreen after a user gesture, so the engine always starts
    windowed.

**Step** (`servers/rendering/renderer_viewport.{h,cpp}`, `rendering_server*`, `viewport`, `scene_tree.cpp`):
- **API.** It is exposed as:
  - `Viewport.scaling_3d_dynamic_step`;
  - `RenderingServer.viewport_set_scaling_3d_dynamic_step()`;
  - the project setting `rendering/scaling_3d/dynamic_step`, applied to the root viewport as the other settings are.
- **Range.** The default is 0.05 and the value is clamped to 0.01-0.25.
- **Where it is used.** `DynamicScale::step` replaces the old `DYNAMIC_STEP` constant everywhere, on both the GPU-time
  and the frame-time path:
  - the grid that `_dynamic_quantize_down()` rounds to;
  - one step up;
  - the minimum step down;
  - the frame-time path's verify loop;
  - the failed-scale keys.
- **Changing the step** clears the failed-up-step memory, because its keys are grid positions. It restarts the
  measurements (not the startup), as the min and max setters do.
- **The default is unchanged in arithmetic.** It is the same float 0.05, now read from a member instead of a
  `constexpr`.

**Docs:** `Viewport.xml`, `RenderingServer.xml` and `ProjectSettings.xml`. `--doctool .` changed only those three
files.

## Verification (actual numbers)

RTX 4080 SUPER on a 2560x1440 display at 165 Hz. The bench is task 75's, with these additions (in `bench/`):
- `step=`, `wm=fullscreen` and `wm2_at=` in `main.gd`;
- `--queries a|b|...` (several loads in one browser profile) and fullscreen in `run_web.mjs`;
- `native_80.sh` and `web_80.sh`. `rate.py` is task 75's, unchanged (`results/75-dynamic-resolution-controller/bench/`).

Adapter asserted in every Chrome run: `nvidia/lovelace`, not the fallback adapter, 48 sampled textures,
`timestamp-query`. Canvas 1920x1080. 0 console errors in every web run.

### Remember: windowed and fullscreen keep separate scales

**Native, real display** (`data/native_remember_display.txt`). Each run is a few seconds, with target 1 fps and
min = max to force the saved value:

| run | logged | start scale (size) |
|---|---|---|
| save windowed (0.70) | `remembered scale 0.70 for /root (windowed)` | |
| save fullscreen (0.60) | `remembered scale 0.60 for /root (fullscreen)`; file holds both keys | |
| load windowed | `starts from its remembered scale 0.70 (windowed)` | **0.70** (1920x1080) |
| load fullscreen | `starts from its remembered scale 0.60 (fullscreen)` | **0.60** (2560x1440) |
| windowed, fullscreen at 1 s | 0.70 (windowed), then `0.60 (fullscreen)` when the mode changed | 0.70 → **0.60** |
| old file `/root=0.65`, windowed | `moved the remembered scale 0.65 of /root to windowed`; file now `/root@windowed=0.65` only | **0.65** |
| then fullscreen | nothing remembered for fullscreen | **1.0** (the old key seeded one mode only) |

**Web, Chrome, 4 loads in one browser profile** (IndexedDB; `data/web_remember_modes.txt`):
1. Windowed: saved 0.70 (windowed).
2. Started from 0.70 (windowed). After `canvas.requestFullscreen()` it settled and saved 0.60 (fullscreen).
3. Windowed: started from **0.70**.
4. Started from 0.70 (windowed), then went fullscreen. It logged `starts from its remembered scale 0.60
   (fullscreen)` and the scale went to **0.60**.

In Chrome the canvas size did not change on fullscreen, because Playwright fixes the viewport. So it was the
every-30-polls check that found the mode, not a resize.

### The step changes the change size

| where | step | changes |
|---|---|---|
| native, Xvfb `--fixed-fps 60`, climbing from 0.5 with room (14 s) | 0.05 | 0.55 0.60 0.65 0.70 0.75 |
| | 0.10 | 0.60 0.70 0.80 0.90 1.0 |
| | 0.25 | 0.75 1.0 |
| Chrome, GPU-bound, load drop at 45 s (back up to 1.0) | 0.05 (default) | 0.8 → 0.85 → 0.9 → 0.95 → 1.0 (4 changes) |
| | 0.10 | 0.8 → 0.9 → 1.0 (2 changes) |
| | 0.20 | 0.8 → 1.0 (1 change) |

In every Chrome step run the down-step landed on 0.80 at 4.4 s, and there were 0 changes/min in both settled windows.

### Defaults behave as in task 75 (Chrome, release template)

| run | result | task 75 (lead, merged tree) |
|---|---|---|
| GPU-bound | settled 0.80 at 4.4 s; 0 / 0 changes/min settled; back to 1.0 1.4 s after the load drop; STABLE 95% | 0.80 within 4.6 s, 1.0 within 1.4 s, 0 changes/min |
| CPU-bound (24 ms busy-wait) | 1.0, `CPU_LIMITED` 94%, 0 changes | `CPU_LIMITED`, 0 changes |

### Off = bit-identical (`data/off_identical.txt`)

`off_batch.sh` compared this branch against the pre-change editor:
- The pre-change editor was built from `f3c673deb2` first and copied aside.
- Xvfb, `--fixed-fps 60`, md5 of 12 float frames after 120.
- **9/9 IDENTICAL**: Forward+ Bilinear 1.0 and 0.77, FSR 2 1.0 and 0.77, TAA 0.77, Bilinear + AE, FSR 2 + AE; Mobile
  Bilinear and Bilinear + AE.

### Builds

- **Editor `dev_mode=yes`: 0 warnings.** Its first attempt caught a `-Wshadow` (a local named `parent`), now fixed.
- **Web `template_release` and `template_debug`** (nothreads, no dlink, raycast): 0 warnings.
- `pre-commit run` on the changed files: clean.

### Not run (per the test scope)

- `smoke.sh`: no shader changed.
- Firefox, the frame-time (withheld `timestamp-query`) runs, and native timing runs. The step is shared code on both
  paths, and the GPU-time path was exercised.
- perf_guard, the Forward+ matrix, threaded or dlink templates.

## Limits and risks

- **Under Xvfb a fullscreen request does not stick.** There is no window manager, so X11's `_NET_WM_STATE` is never
  confirmed: the first frame reported fullscreen, and the next ticks reported windowed. The per-mode native checks
  therefore ran on the real display. They are short runs (2-8 s each), and none of them is a timing run.
- **A mode change restarts the controller from the remembered scale.** It does so only when that mode has a saved
  value. The resize that comes with a mode change reconfigures the buffers anyway.
- **The step changes the grid.** A scale saved at 0.05 steps (for example 0.85) is used as is, then quantized to the
  new grid at the next change.
- **On the web**, fullscreen always starts from the windowed key and switches once the page goes fullscreen. The
  windowed scale is used for the first frames.

## What the lead must still run

Merge, then the dev_mode editor and the web `template_release`. To rerun:
- `bench/native_80.sh` (Xvfb; `WHERE=display NOSTEP=1` for the per-mode part);
- `bench/web_80.sh` (gpu lease; it opens a fullscreen Chrome on the screen);
- `bench/off_batch.sh` (needs `$S/pre/`).

Edit `S=` in the scripts, and the template paths in `export_presets.cfg`.
