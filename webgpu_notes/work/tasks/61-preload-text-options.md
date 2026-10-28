# 61-preload-text-options: the start screen's download text, configurable

State: open
Owner: worker-61
Claimed:
Needs: build (editor + web template), browser (Chrome and Firefox)
Branch: work/61
Results: webgpu_notes/work/results/61-preload-text-options/

User request, 2026-10-28: "For the preload, make the 'downloading X%' and 'ready' text, etc all able to be turned off,
the text edited, and able to set the colour."

Today (task 53, TASKS §4.63) the start screen shows, under the Start button while `html/start_button/preload` and
`preload_progress` are on: "Downloading..." (total size not known yet), "Downloading N%" (never backwards), then "Ready"
(nothing on failure). Hard-coded in both default shells (`misc/dist/html/full-size.html`, `webgpu-full-size.html`,
`statusPreload`).

## Goal

Web export options, shown only while `html/start_button/enabled`, `preload` and `preload_progress` are on
(`get_export_option_visibility()`, `update_visibility` on the switches, as task 51/53 did), passed in `$GODOT_CONFIG`
(`startButton`, documented in `config.js`), applied in both default shells:

- **Each message separately switchable and editable:**
  - the unknown-size message (default "Downloading...");
  - the progress message (default "Downloading {percent}%"; `{percent}` is replaced by the integer percentage, documented;
    a text without `{percent}` is shown as is);
  - the ready message (default "Ready").
  A message switched off shows nothing in that state (the line stays empty, layout does not jump).
- **Text colour** for the line (default: what it is now, so existing exports render identically), and keep the shell's
  font and size; if the current colour comes from the shell's CSS, read it there and make that the default.
- Naming next to the existing `html/start_button/*` options (e.g. `html/start_button/preload_text/...`); say what you chose.
- Defaults leave an existing export byte-identical in what it shows (pixel-identical start screen in Chrome and
  Firefox) and the same config otherwise; old presets without the new keys load with the defaults.
- Text is inserted with `textContent`, never `innerHTML` (the config is author-controlled, but keep it safe).

## Verification

1. Option visibility in the export dialog (task 51's visibility addon or the windowed editor), each switch hiding its text.
2. Chrome and Firefox (launch rule, adapter asserted), throttled download as task 53's `preload_test.mjs` does: each
   message on and off, edited texts (including `{percent}` in the middle, and a text without it), a colour, and the
   defaults pixel-identical to the current start screen.
3. dev_mode editor build, web template_debug **and template_release** (§3 trap 95), smoke not needed (no shaders).
4. for-docs.md: the wiki's Web Export Options start-screen section updated.
