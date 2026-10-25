# 18-camerasim-look: the user's cameraSim project, WebGPU (SPIRV-Cross) beside native Vulkan, for a by-eye check

State: ready-for-integration
Owner: worker-18
Claimed: 2026-10-25 15:00
Needs: gpu, build (only if a template has to be rebuilt)
Branch: work/18
Results: webgpu_notes/work/results/18-camerasim-look/

## Context

User bar (2026-10-25): before Tint is deleted (task 09), the user judges their real test project by eye: "Current bar
plus my cameraSim project by eye". The project is `/home/schme16/Downloads/cameraSim_2026-09-09_10-26-06/testing`
(Forward+, main scene `uid://etk7cti113sw`, which holds the Ava MetaHuman with its hair and skin shaders). Task 05
converted all its stages (valid, interface-identical); this task is about how it *looks*.

**Never modify the user's project.** Work on a scratch copy (exports write into `.godot/`); see
`webgpu_tests/screenshot_comparison/LIVE_REPRO_METHODOLOGY.md` for the scratch-copy, native capture and web capture
recipe, and `webgpu_tests/spirv_cross_eval/project_export.sh` (task 05) for exporting this project with the baker.

## Goal

Frame pairs of the main scene, native Vulkan (the lead's `bin/godot.linuxbsd.editor.x86_64` running the scratch
project with `--rendering-driver vulkan`) and the WebGPU export (lead's `bin/` editor + nothreads release template,
baked, headed system Chrome with the Vulkan flags, real GPU adapter verified), from the same camera at the same time
into the scene:

- 3-5 views: the default view after it settles, a close-up of the MetaHuman's face and hair, and any other views the
  scene offers that exercise different effects (look at the scene and its scripts to find them). If the scene is
  animated or interactive, hold the camera/time fixed the way LIVE_REPRO_METHODOLOGY describes, or capture at a fixed
  frame number on both sides; say in SUMMARY how the pairs were aligned.
- Each pair as one side-by-side PNG (native left, WebGPU right, labelled), max ~1600 px wide, in
  `results/18-camerasim-look/looks/`.
- Also record per view the mean absolute difference, the web console's errors/warnings (and the shader stats line:
  baked / translated), and the export's load time.
- Anything that differs visibly: one line on what and where, and whether it also differs with the Tint-baked converter
  (`bin/tint_convert_cli.tint-pre-swap` in the main checkout, used as the baker the way task 08/12 did) — that tells
  the lead whether it is the swap or older.

Don't fix rendering differences in this task; report them. The lead posts the pairs on the hand-offs page.

## Verification

SUMMARY.md with the table (view / mean diff / notes) and the PNG list; smoke.sh not needed unless you change code.
