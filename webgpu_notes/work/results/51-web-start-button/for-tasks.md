# For TASKS.md (task 51)

## §2.51 → §4: proposed archive entry

### 4.xx Web start screen: `html/start_button/*` (October 2026) *(work task 51; §2.51)*

User request (camera-sim's start screen, moved into the default shell). Web export option `html/start_button/enabled`
(default off) opens the page on the boot splash with a Start button; **nothing of the engine runs until it is pressed**
(no engine download or compile, no GPU device, no audio context), then the usual startup runs with the same loading
screen. The missing-features check stays at page load; the button is not focused on load (in an iframe that scrolled the
parent). Shown only while enabled: `text`, `background_color`, `text_color`, `border_color` / `border_width` /
`border_radius`, `font_family`, `font_size`, `padding` (Vector2i), `custom_state_colors` with `hover_color` /
`pressed_color` (shown only while that is ticked; otherwise the export darkens the background 15% / 30%). `cursor:
pointer`, a `:focus-visible` ring in the text color, Enter/Space (a real `<button>`). Passed to shells as
`GODOT_CONFIG.startButton` (no new placeholder; `EngineConfig.startButton` documents it; the config JSON now escapes `<`).
Both default shells (`full-size.html`, `webgpu-full-size.html`). Preload is task 53.

## Questions for the user (defaults marked)

1. **Default button look.** Background `#3a3a3a` (neutral dark grey), white 24 px Noto Sans text, no border, 8 px
   radius, padding 48 x 12 px; hover 15% darker, pressed 30% darker. On a dark grey that darkening is subtle.
   *Default: keep.* Alternative: lighten on hover for dark backgrounds (pick by luminance).
2. **Cursor option.** `cursor: pointer` is fixed; no option was added (no use seen). *Default: no option.*
3. **Accessible name.** The button's text is its accessible name; no separate aria-label option. A text such as "▶"
   would read badly. *Default: no option; add `html/start_button/accessible_name` if wanted.*

4. **Pressed shade for Space in Firefox.** Firefox does not apply `:active` while Space is held on a button (Chrome
   does; mouse presses darken in both). *Default: leave it to the browser.* Alternative: a few lines toggling a class on
   keydown/keyup.

## Note for task 53

`for-task-53-startup-hold-prototype.patch` (this folder) is an **untested** prototype written before the spec changed:
the engine runs its startup through `Main::setup2()` (servers, renderer, type registration: no project code) and holds
before `Main::start()` until the page releases it (`godot_js_os_startup_hold()` polled from `startup_step_callback()`,
`Engine.prototype.preloadGame()`, `Module.resumeAudio()` called inside the Start click). It goes further than a
prefetch (it also creates the GPU device and audio context before the press, which the task 51 spec forbids for the
plain start screen). It never ran; take it only as an idea for how far a preload could go.
