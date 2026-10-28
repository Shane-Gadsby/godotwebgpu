# Task 80: for the docs

## Wiki: `Engine-Additions-Dynamic-Resolution.md`

**In "The settings" table**, add a row after `scaling_3d_dynamic_max_scale`:

| `scaling_3d_dynamic_step` | `rendering/scaling_3d/dynamic_step` | 0.05 | The size of each change (0.01-0.25). Scales are multiples of it. A step up is one step; a step down goes straight to the scale that fits, rounded down to a multiple. Smaller follows the load more closely, with more but less visible changes. Larger changes less often, and each change shows more. |

**In "Remembering the scale between runs"**, replace the second sentence ("It goes in ... keyed by renderer, GPU and
viewport path.") with:

> It goes in `user://scaling_3d_dynamic.cfg`, keyed by renderer, GPU, viewport path and window mode. Windowed (also
> maximized), fullscreen and exclusive fullscreen each keep their own scale, because they are different pixel counts
> and often different refresh rates. The mode is that of the window the viewport is shown in; for a SubViewport, its
> window's. When the mode changes while the game runs, the scale saved for the new mode is put back.
>
> On the web, a page can only go fullscreen after a click or key press, so a game starts from the windowed scale and
> switches when the page goes fullscreen.
>
> Files saved before window modes were part of the key still work. The first mode that starts takes the saved scale,
> once, and the file is rewritten with the mode.

**Under "To start over"**, add: the keys look like `/root@windowed` and `/root@fullscreen`.

## `webgpu_site/` and `drivers/webgpu/README.md`

Nothing changes there.
