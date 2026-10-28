# For TASKS.md (work task 70)

## §4 entry

### 4.xx Physical DOF follow-ups: 6% max blur, legacy gather (October 2026) *(work task 70; camera-sim spec)*

User answers on task 63's questions. `rendering/camera/depth_of_field/depth_of_field_physical_max_blur_size` now
defaults to **6%** of the image height (was 3%; opt-in cameras only). New **`CameraAttributesPhysical.dof_legacy_gather`**
(default off, shown once `dof_physical_blur_size` is on; RS `camera_attributes_set_dof_legacy_gather`): the thin-lens
size drawn through upstream's gather (fixed 32 px reach, Box/Hexagon dilution of §3 trap 97, half-size passes twice as
wide), for projects tuned through it; documented as not physically correct. No shader, binding or push-constant change.
Worker: opt-in off/defaults bit-identical to the pre-change editor, 14/14 (Forward+, Mobile) and 4/4 web; 200 mm f/2.8
at 1440p (model 80.4 px) 42.8 at 3% → 79.5 (F+) / 80.0 (Mobile) at 6%; legacy = pre-change upstream path through
size-matched settings (camera-sim's own pre-63 setup), 20/20 native and 4/4 web maxdiff 0. WebGPU 1440p, upstream /
3% / 6%: Hexagon Low 0.54 / 0.35 / 0.42 ms, High 1.42 / 1.50 / 1.54, **Circle Medium 3.97 / 7.83 / 34.74** (reach
squared; documented as too slow for real time above 1080p). Task 63's missing Circle 6% number was `gpu_time.mjs`'s
60 s window per 2048 command buffers at ~70 ms/frame, not a hang. At 6% Box/Hexagon each quality draws small blurs like
the next lower one at 3%; Very Low misses blurs under ~1% of the image height (documented; question `dof-very-low-6pct`).
Lead (merged tree): ...

## §3 trap candidates

- **A web export whose project viewport is larger than the page renders at the project size and downscales** (task
  70): task 63's web test project sets 2560×1440 and was shot in a 1280×720 page, so it rendered at 1440p. A
  frame-relative blur doubled in buffer pixels against a fixed-pixel reference (maxdiff 28, an hour of suspecting the
  new code). Print or read the buffer size the effect actually runs at before comparing pixel-sized settings across
  platforms.
- **`gpu_time.mjs` gives up on a slow frame:** each round waits 60 s for 2048 command buffers (~1024 frames at two per
  frame), so anything over ~55-60 ms per frame produces no number, which reads like a hang (task 63's Circle at 6%).
  Use `--passes` (a ~18-frame window) for very slow configurations.

## Questions for the user

- `dof-very-low-6pct` (choice): at the 6% default, Box/Hexagon spread their fixed sample count over twice the reach,
  so Very Low draws a blur under ~1% of the image height almost sharp (720p, 5.5 px blur: 2.2 px wide against 7.6) and
  every quality looks like the next lower one did at 3%.
  - **Document it (default)**: shipped; the setting's description says to use Low or higher.
  - Scale the Box/Hexagon sample count with the reach (6% = twice the samples at 3%'s density; Hexagon Low ~0.4 →
    ~0.8 ms at 1440p).
- `dof-circle-6pct` (choice): Circle Medium at 6% costs 34.7 ms per frame at 1440p on the 4080 SUPER (9× upstream).
  - **Document it (default)**: shipped; the docs point to Box/Hexagon or a smaller value.
  - Keep the Circle's reach at 3% while Box/Hexagon reach 6% (bigger Circle blurs clamp visibly).

## Note for the camera-sim session

Engine side on work/70 (pending integration), on top of task 63's note in `results/63-physical-dof-blur-size/for-tasks.md`:

- To keep camera-sim's tuned look while moving to `CameraAttributesPhysical` + `dof_physical_blur_size = true`, also
  set **`dof_legacy_gather = true`** (Inspector: Depth of Field > Legacy Gather). Verified: legacy renders the same
  pixels as the pre-63 path camera-sim uses today (Practical attributes, transitions −1, `dof_blur_amount =
  max_blur_px / 64`), when the sizes match. camera-sim sizes the blur against the sensor width and viewport width and
  the engine against the sensor height and buffer height; with the 16:9 sensor strip of task 63's note they agree.
- Leave it off for the physically correct look (the stronger blur at wide apertures; f/1.4 at 2.5 m keeps ~1/6 of the
  face's edge energy, task 63's A/B).
- The max blur default is now 6%: a long lens wide open no longer clamps. With the legacy gather the reach stays 32 px
  as before, so nothing changes for camera-sim there. Without it, avoid Circle above 1080p (34.7 ms at 1440p) and
  Box/Hexagon Very Low.
