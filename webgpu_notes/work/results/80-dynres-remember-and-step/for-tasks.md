# Task 80: for TASKS.md

## §4 entry: append to §4.91, or add it as §4.92

**Dynamic resolution: remember per window mode, and the step exposed** *(work task 80; the user's answers on task 75's
`dynres-remember-key` and `dynres-step-size`)*.

- **Remember per window mode.** `scaling_3d_dynamic_remember` now keys `user://scaling_3d_dynamic.cfg` by
  `<viewport path>@<window mode>` within the renderer + GPU section:
  - the modes are windowed (also maximized), fullscreen and exclusive fullscreen;
  - the mode is that of the window the viewport is shown in.
- **Old files.** A task 75 key without the mode seeds the first mode that loads, once. It is then rewritten with the
  mode, and the old key is erased.
- **Mode changes while running** load that mode's saved scale. The mode is asked of the display server only when the
  window's size changed, or every 30 polls.
- **The step** is exposed as:
  - `scaling_3d_dynamic_step` on `Viewport`;
  - `viewport_set_scaling_3d_dynamic_step` on `RenderingServer`;
  - `rendering/scaling_3d/dynamic_step`.

  The default is 0.05, the range 0.01-0.25. It is used by the GPU-time and frame-time paths alike: grid, up-step,
  minimum down-step, verify loop.
- **Worker verification:**
  - Remember:
    - natively on the real display, windowed 0.70 / fullscreen 0.60 round-trip;
    - a switch to fullscreen while running loads 0.60;
    - an old `/root=0.65` moved to windowed only;
    - in Chrome, the same across four loads in one profile.
  - The step changes the change size:
    - natively, 0.05 / 0.10 / 0.25 climb in steps of exactly that;
    - in Chrome, 1.0 is reached after the load drop in 4 / 2 / 1 changes for 0.05 / 0.10 / 0.20.
  - Defaults as in task 75 (Chrome): 0.80 at 4.4 s, 0 changes/min, 1.0 within 1.4 s of the load drop; CPU-bound
    `CPU_LIMITED` 94% with 0 changes.
  - Off is bit-identical 9/9.
  - The dev_mode editor and the web release and debug templates build with 0 warnings.

## Trap candidates

- **`git stash` is repo-wide across worktrees** (already in the board README since 2026-10-29). Worker 80 used it
  to build a pre-change editor and popped worker 79's stash. The safe way to build a "before" binary is a commit
  on your branch, then `git checkout <base> -- <files>` and back, or a separate worktree.
- **Under Xvfb, X11 fullscreen does not hold.** There is no window manager to confirm `_NET_WM_STATE`, so the next
  frames already report windowed, and the window size does not change either: Xvfb's screen is the window's size.
  Any check that depends on the window mode needs the real display, or a WM inside the X server; none is installed
  (only Xephyr, which has none either).
- **Fullscreen in a Playwright web run:**
  - a mouse click on the canvas did not reach `_input` in the bench;
  - Godot's own `window_set_mode(FULLSCREEN)` needs a gesture inside an input callback;
  - what works is `page.evaluate(() => document.querySelector('canvas').requestFullscreen())`, which Chromium counts
    as a gesture;
  - the canvas must be the fullscreen element, because Godot's `fullscreenchange` listener checks `evt.target ===
    canvas`.

  The canvas size stays at Playwright's fixed viewport.

## Questions for the user

- `dynres-remember-subviewport-mode`: for a SubViewport shown inside an embedded Window, remember uses the mode of
  the host window on the display, not the embedded window's own mode flag (which says nothing about the screen).
  **Default: keep it that way.** The alternative is to use the embedded window's own mode.
