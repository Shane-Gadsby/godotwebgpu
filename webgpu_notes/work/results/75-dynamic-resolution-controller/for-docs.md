# Task 75: for the docs (lead copies in)

## Wiki: Engine Additions, new page "Dynamic 3D Resolution"

### What it does

Dynamic 3D resolution picks a viewport's 3D render scale at run time so the game holds a frame rate. When the GPU
cannot keep up, the scale goes down. When the GPU has room again, the scale goes back up. 2D and UI always render at
full resolution. The upscaler you already chose stays in use: Bilinear, FSR 2 or TAA.

It only lowers the scale when resolution is the problem. If frames miss the target while the GPU has time to spare,
the CPU is setting the pace. Rendering fewer pixels would not help, so the scale stays where it is and the viewport
reports **CPU-limited**.

Changes are slow and deliberate. A change waits until the previous one has settled, and once the scale has settled it
rarely moves: in our tests, 0 changes in 30-45 s windows with GPU timing.

A scale change no longer resets anything visible:
- auto exposure carries on, with no flash;
- FSR 2 keeps its history, because its context is made once at the largest size the controller can pick;
- TAA resamples its history into the new size.

### How to turn it on

For the main window, open **Project Settings > Rendering > Scaling 3D** and turn on **Dynamic**.

For any other viewport, set it from a script:

```gdscript
var vp := get_viewport()            # or a SubViewport
vp.scaling_3d_dynamic_target_fps = 60.0
vp.scaling_3d_dynamic = true
vp.scaling_3d_effective_scale_changed.connect(func(scale): print("3D scale now ", scale))
```

`scaling_3d_scale` is the **starting** scale. The controller never changes that property. Read the scale actually in
use with `get_scaling_3d_effective_scale()`.

### The settings

| Viewport property | Project setting (main window) | Default | Meaning |
|---|---|---|---|
| `scaling_3d_dynamic` | `rendering/scaling_3d/dynamic` | off | On or off. Off behaves exactly as before. |
| `scaling_3d_dynamic_target_fps` | `rendering/scaling_3d/dynamic_target_fps` | 60 | The frame rate to hold. |
| `scaling_3d_dynamic_min_scale` | `rendering/scaling_3d/dynamic_min_scale` | 0.5 | Never render below this scale. |
| `scaling_3d_dynamic_max_scale` | `rendering/scaling_3d/dynamic_max_scale` | 1.0 | Never render above this scale. Above 1.0 supersamples when there is room (Bilinear only; other modes stop at 1.0). |
| `scaling_3d_dynamic_remember` | `rendering/scaling_3d/dynamic_remember` | off | Save the settled scale and start from it next time (see below). |

These are read-only:
- `get_scaling_3d_effective_scale()`: the scale in use.
- `get_scaling_3d_dynamic_state()`: one of `OFF`, `STARTING` (the first 3 s, while shaders compile), `ADJUSTING`,
  `STABLE`, `CPU_LIMITED` or `AT_MIN_SCALE`.
- `get_scaling_3d_dynamic_source()`: `GPU_TIME` or `FRAME_TIME`, explained below.

The signal `scaling_3d_effective_scale_changed(scale)` fires on every change. The state is what a settings menu can
show, for example "Auto (1080p)" or "Limited by CPU".

The same controls exist on `RenderingServer` as `viewport_set_scaling_3d_dynamic*` and
`viewport_get_scaling_3d_effective_scale` / `_dynamic_state` / `_dynamic_source`. The getters are cheap enough to call
every frame.

### Remembering the scale between runs

With `scaling_3d_dynamic_remember` on, the scale is saved when the viewport becomes `STABLE` and the next run starts
from it. It goes in `user://scaling_3d_dynamic.cfg`, keyed by renderer, GPU and viewport path.

`user://` is per machine. On the web it is per browser, because it lives in the browser's IndexedDB.

To start over, delete the file, or the viewport's key in it. Your own "Auto" button can do the same.

To save it yourself instead, read `get_scaling_3d_effective_scale()` when the state is `STABLE`. Next run, set
`scaling_3d_scale` to the saved value before you turn `scaling_3d_dynamic` on.

### How it decides, and the limits

**With GPU time** (source `GPU_TIME`) it measures how long the GPU spends on the viewport. It lowers the scale when
that exceeds 95% of the frame budget, raises it when the next step would stay under 80%, and changes nothing in
between. This is the precise path. GPU time is available on:
- Vulkan, Direct3D 12 and Metal;
- desktop OpenGL (Compatibility);
- WebGPU, when the browser offers `timestamp-query`. Current Chrome and Firefox both do.

**Without GPU time** (source `FRAME_TIME`) it works from frame time. That covers WebGL, OpenGL ES, and WebGPU without
`timestamp-query`. It has three habits:
- It tries a step down. If the frame does not get faster, it puts the scale back and reports `CPU_LIMITED`.
- It tries a step up, and steps back if that misses. That scale is not tried again for a while: 10 s at first,
  doubling up to 2 minutes.
- On WebGPU, how many frames the browser has queued often settles "GPU- or CPU-bound?" at once, so many of these
  tries are skipped.

The frame-time path is slower to settle and moves more often while it learns. On a display whose refresh rate is at
or below the target (a 60 Hz screen with a 60 fps target), frame time cannot show spare room. In that case this path
can only hold a scale that works and occasionally try the next one.

**Per renderer:**

| Renderer | Scaling modes | GPU time |
|---|---|---|
| Forward+ (desktop, web) | Bilinear, FSR 2, TAA; FSR 1.0 natively | yes (web: with `timestamp-query`) |
| Mobile | Bilinear (FSR 1.0 natively) | yes on native |
| Compatibility | Bilinear | desktop OpenGL yes; WebGL / OpenGL ES no (frame time) |

**Other limits:**
- **FSR 1.0 on the web (WebGPU)** falls back to Bilinear while the controller is on, with a warning.
- **XR viewports**: not supported; the controller stays off there, with a warning.
- **A change reallocates the 3D buffers.** On the web that can cost one longer frame (25-70 ms in our measurements),
  which is why changes are kept rare.
- **Several dynamic viewports** each use their own GPU time. Without GPU time they share one frame time, so they take
  turns: one changes at a time, largest first.
- **Volumetric fog** restarts its temporal smoothing on each change.
- **The editor** never applies the project setting to its own viewports.

## webgpu_site / TECHNICAL_REFERENCE.md (a short section, lead's call where)

Dynamic 3D resolution lives in `RendererViewport` (`servers/rendering/renderer_viewport.cpp`, `_dynamic_scale_*`). It
runs once per frame, before the viewports draw, from each viewport's `vp_begin`/`vp_end` GPU timestamps (task 73's
`timestampWrites` readback on WebGPU) or the frame interval. FSR 2 sizes its context from
`RenderSceneBuffersConfiguration::max_internal_size`, so controller changes never grow it (task 74). On WebGPU the
fallback also reads `RDD::get_submit_to_done_latency_usec()`: one `onSubmittedWorkDone` per frame, started only once the controller asks, kept in its own
in-order FIFO rather than on the frame fence. The fence is resubmitted while its previous callback can still be
pending, so it would pair callbacks with the wrong submits. The design and measurements are in
`webgpu_notes/work/results/68-dynamic-resolution/DESIGN.md` and `results/75-dynamic-resolution-controller/`.
