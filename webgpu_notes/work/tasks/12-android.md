# 12-android: Chrome and Firefox on the Razr 60, works and is as fast as Tint

State: open (after 07's first reading)
Owner:
Claimed:
Needs: gpu (the phone counts as the gpu lease too), the Razr 60 over adb
Branch: work/12
Results: webgpu_notes/work/results/12-android/

## Context

User bar (2026-10-25): Chrome on Android and Firefox must work, ideally as fast as or faster than with Tint. The phone
recipe (reverse port + raw CDP, worker instrumentation, screencap traps) is in TASKS.md §2.9 and the memory "Android
phone profiling". Mobile GPUs are where WGSL shape (private-variable stage I/O, helper functions, `const` arrays vs
`var<private>` — task 06's finding) is most likely to cost time.

## Goal

The same Tint-baked and SPIRV-Cross-baked exports (Forward+ fixture, the smoketest's platformer and heavy scenes) on
the phone in Chrome and Firefox for Android: loads, renders correctly (screenshots beside the desktop Vulkan
reference for the user's Look strip), and frame time / GPU time per frame no worse than Tint. Anything slower:
find the construct, patch.

## Verification

Per browser: works yes/no, frame time Tint vs SPIRV-Cross (mean and spread, three runs), screenshots for the lead to post.
