# Task 62: wiki text for the Camera Finishing and Monitoring page

Add as a new section after the existing "Histogram" overlay section.

---

## Reading the histogram from a script

The histogram overlay is counted on the GPU from the finished image. A script can read the same counts, for a
histogram drawn in its own UI, an exposure meter, or a clipping warning, without reading the frame back itself.

| Camera3D | Default | What it does |
|---|---|---|
| `monitoring_histogram_enabled` | `false` | Counts the histogram (and draws it, below). Nothing happens while it is off. |
| `monitoring_histogram_draw` | `true` | Draws the histogram over the image. Off: counted for scripts only, nothing drawn. |
| `monitoring_histogram_update_interval` | `0.0` | Seconds between two readbacks; `0.0` = every frame. |
| `get_monitoring_histogram(channel = LUMA)` | | 256 counts for Y', R', G' or B' (`MONITORING_HISTOGRAM_CHANNEL_*`), the latest readback. |
| signal `monitoring_histogram_updated` | | A new readback has arrived. |

```gdscript
@onready var camera: Camera3D = $Camera3D

func _ready():
    camera.monitoring_histogram_enabled = true
    camera.monitoring_histogram_draw = false          # The UI draws its own.
    camera.monitoring_histogram_update_interval = 0.25 # 4 updates a second is plenty for a panel.
    camera.monitoring_histogram_updated.connect(_on_histogram)

func _on_histogram():
    var luma := camera.get_monitoring_histogram()      # PackedInt32Array, 256 bins.
    var bins64 := PackedInt32Array()
    bins64.resize(64)
    for i in 256:
        bins64[i / 4] += luma[i]
    var highlights_clipping := false
    for i in range(250, 256):
        highlights_clipping = highlights_clipping or luma[i] > 0
    # ... redraw the panel
```

**What is counted.** The display-encoded image (the values as they are shown, nonlinear), after tonemapping and the
CameraAttributes noise reduction and sharpening, and **before** the monitoring overlays: zebras, false color, focus
peaking and the histogram itself never change the counts. Bin `i` counts the pixels whose value rounds to `i / 255`.
Luma is Rec. 709 Y' of the display-encoded R'G'B'. Every channel adds up to the width x height of the viewport's 3D
render (divide by it for fractions of the frame).

**When it runs.** The readback starts the first time the script calls `get_monitoring_histogram()` or connects to
`monitoring_histogram_updated`, and runs while the histogram is enabled, whether or not it is drawn and whether or not
**Visible in Output** is on (that option still only decides what is drawn in the running project). A project that only
draws the histogram, or only in the editor, reads nothing back. With drawing off, the image is counted only on the
frames a readback is due, so at `0.25` s the counting costs a compute pass four times a second.

**It never stalls.** The counts are copied to a staging buffer inside the frame and handed back when the engine next
waits for that frame anyway (natively), or when the browser has mapped the buffer (web, WebGPU `mapAsync`). Results
are one or more frames old. On the web the first frames return an empty array; check `is_empty()` or wait for the
signal.

**One histogram per camera.** Each camera has its own counts: two SubViewports with their own cameras each read their
own. A camera rendered into two viewports in the same frame returns whichever was read back last.

**Limits per renderer.** Forward+ and Mobile, desktop and web (WebGPU). Compatibility has no compute shaders, so the
histogram is neither drawn nor counted there and `get_monitoring_histogram()` returns an empty array. Not in XR
(multiview) rendering.

`RenderingServer` has the same as `camera_set_monitoring_histogram_readback(camera, enable, interval)`,
`camera_get_monitoring_histogram(camera, channel)` and `camera_get_monitoring_histogram_serial(camera)`; the two
getters are thread-safe and do not wait for the rendering thread.
