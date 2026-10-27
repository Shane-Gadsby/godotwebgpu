# 37-camera-finish-monitoring: in-camera noise reduction and sharpening; monitoring overlays on Camera3D

State: claimed
Owner: worker-37
Claimed: 2026-10-28 (worker-37)
Needs: build, gpu
Branch: work/37
Results: webgpu_notes/work/results/37-camera-finish-monitoring/

Read `webgpu_notes/work/camera-batch.md` first.

## Goal

A new "camera finish" pass after tonemapping (it needs the display image and neighbors), at output resolution, skipped
entirely when nothing in it is on.

CameraAttributes (user's call):
1. **In-camera noise reduction**: edge-preserving (bilateral or a small non-local-means), strength, optional chroma-only
   mode (cameras reduce chroma noise harder than luma); works on the grain/noise of §4.47 realistically (it should be
   able to clean digital noise and smear fine detail at high strength, as cameras do).
2. **In-camera sharpening**: unsharp mask / contrast-adaptive (cite), amount and radius (radius relative to frame, not
   pixels), halo control.

Camera3D (user's call), new group "Monitoring":
3. **Zebras**: diagonal stripes over luma above a threshold (IRE %, default 95, second zebra optional at 70%),
   animated like a camera's.
4. **Focus peaking**: edges in focus highlighted (color, sensitivity), from an edge detector on the display luma (or
   better, the DOF CoC where available: in-focus = CoC below a pixel).
5. **False color**: exposure map (the ARRI/Atomos-style IRE bands: purple/blue under, green middle gray, pink skin,
   yellow/red near clipping; cite the band table used).
6. **Histogram**: luma or RGB, drawn in a corner (position, size, opacity), computed on the GPU (compute where
   available; Compatibility: document if not).
7. **Visible in Output** check box (default off): off = overlays only in the editor (when the editor renders through
   this camera: the 3D viewport camera preview and the Camera3D preview), on = also in the running game.

Plumbing: Camera3D -> RenderingServer camera API (e.g. `camera_set_monitoring(...)`) -> RendererSceneCull -> render
data -> the finish pass. In a running game with Visible in Output off, nothing is sent and nothing runs. Overlays must
not be written into anything other passes read back (they are the last thing drawn).

## Verification

Default bit-identical. NR: measured noise sigma reduction on a flat patch with digital noise at ISO 6400 vs strength,
and edge preservation on a step. Sharpening: step-edge overshoot vs amount; radius in pixels scales with resolution.
Zebra coverage equals the fraction of pixels above threshold; false color bands match the table on a gray ramp;
histogram bins match a CPU histogram of the same frame within quantization. Editor-only vs Visible in Output checked
in both an editor-hint run and a game run. WebGPU vs native, smoke.
