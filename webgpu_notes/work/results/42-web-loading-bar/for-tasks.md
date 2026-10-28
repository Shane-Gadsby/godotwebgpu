# For TASKS.md (task 42)

- §2.49 → close and move to §4: "Web loading screen progress bar toggle + colors (2026-10-28, work/42)". Export options
  `loading_screen/progress_bar/enabled` (default on) and `border_color` / `background_color` / `fill_color` (shown only
  while enabled; defaults are Chrome's unstyled `<progress>` colors `#b2b2b2` / `#efefef` / `#0075ff`). Passed to shells
  as the new `$GODOT_PROGRESS_BAR` JSON placeholder (`enabled`, `borderColor`, `backgroundColor`, `fillColor`,
  `customColors`), not inside `$GODOT_LOADING_SCREEN`. Both default shells style the bar only when `customColors` is
  true, so default exports render pixel-identically to before (Chrome + Firefox, both shells). Verified on real exports in
  Chrome and Firefox (14/14), see results/42-web-loading-bar/SUMMARY.md.
- Possible §3 trap (measurement): Playwright's Firefox has no WebGPU without `FIREFOX_WEBGPU_PREFS`
  (`webgpu_tests/scene_smoketest/firefox_prefs.mjs`). Without them the default shell shows "WebGPU (try Chrome, Edge, or
  Firefox)" missing. Every ad-hoc script must take its launch options from there and assert the adapter first.

## Questions for the user

None.
