# Camera realism batch (tasks 33-38): shared conventions

User request (2026-10-28): every missing physical/practical camera attribute, after film grain (TASKS.md §4.47) and
camera LUTs (§4.48). Locations decided by the user and lead:

- **CameraAttributes** (the shared base, so Physical and Practical both get it, unless a property only makes sense
  with physical units): sensor format, vignetting, lens distortion, chromatic aberration, anamorphic lenses, ND
  filters and T-stops, diffraction softening, focus breathing, aperture blades/bokeh/cat's eye, tilt-shift, starburst,
  halation, motion blur from the shutter, rolling shutter, AE metering modes, auto white balance, **in-camera noise
  reduction and sharpening** (user's call).
- **Environment** (scene look, driven by scene lights, next to glow): lens flare + veiling glare, diffusion filters.
- **Camera3D** (user's call): autofocus (target node / point, speed, modes; the lens side such as breathing stays in
  CameraAttributes), stabilization / handheld shake, and **monitoring overlays** (zebras, focus peaking, false color,
  histogram) with a **"Visible in Output"** check box: off = drawn only in the editor (editor viewport camera preview,
  Camera3D preview), on = also in the running game.
- Quality knobs (sample counts) that are per project go in ProjectSettings next to the DOF ones, as DOF does.

## Rules for every task in the batch

- **Read the precedents first**: commits `e027938df5` (white balance), `baed93b3d7` (grain), `cb6fa65349` (camera LUT).
  They show every touch point: `scene/resources/camera_attributes.*`, `servers/rendering/storage/camera_attributes_storage.*`,
  `rendering_server{.h,.cpp,_default.h,_enums.h}`, `renderer_rd/effects/tone_mapper.*`, `renderer_scene_render_rd.cpp`,
  `shaders/effects/tonemap.glsl`, `tonemap_mobile.glsl`, the Compatibility `drivers/gles3` post pass, class docs XML.
- **Default off and bit-identical**: a new effect is disabled by default and costs nothing when off (uniform branch or
  skipped pass). Existing projects must render exactly as before (check: pixel diff 0 against the pre-change editor on
  a scratch scene).
- **The Forward+ tonemap push constant is full (128 bytes).** Do not grow it. Grain and LUT bit-packed into `pad` and
  `flags`; there are no bits left worth taking. Task 33 adds a `CameraEffects` uniform buffer to the tonemap pass (all
  three renderers) with reserved regions for tasks 35 and 36; anything else that needs tonemap-pass parameters waits
  for it. A task with its own new pass uses that pass's own push constant.
- **All three renderers**, unless the technique cannot work there; then say so in the class docs (`[b]Note:[/b]` like
  auto exposure's "only supported in Forward+") and in SUMMARY.md. Web (WebGPU Forward+) must work: no features WebGPU
  lacks (no push-constant growth, max 4 bind groups with set 3 shared by push-constant emulation, no 3-channel texture
  formats, no storage-texture formats WebGPU forbids).
- **Physically grounded, and say where numbers come from**: a comment cites the source of each model or constant
  (paper, manufacturer data, standard). Where a value is a look choice, the comment says so. Prefer derivation from the
  camera's existing physical settings (aperture, shutter, ISO, focal length, focus distance, sensor) over free sliders,
  with a strength/override slider on top where users will want control.
- **Resolution correctness**: anything with a spatial size (blur radii, fringes, shake) is defined relative to the
  frame (sensor), not pixels, so it looks the same at every resolution (grain's Selwyn handling is the precedent).
- **Shared files conflict**: the lead merges all six branches. Keep additions in distinct blocks (your own ADD_GROUP,
  your own RS method, your own storage functions with a `/* <Feature> */` comment block, your own include file for
  shader code: `servers/rendering/renderer_rd/shaders/effects/camera_<feature>_inc.glsl` plus a Compatibility copy
  under `drivers/gles3/shaders/effects/`). Don't reformat or reorder other code.
- **Interface references**: a new binding changes `webgpu_tests/spirv_cross_eval/interface_reference.json` and
  `webgpu_tests/wgsl_text_scan/scan_reference.txt`. Regenerate them on your branch so your smoke passes
  (`compare_interface.py --write`, keep the `source` history and append your change; `WRITE_REFERENCE=1 run_tests.sh`),
  list exactly what changed in SUMMARY.md; the lead regenerates again after merging.
- **Builds**: same flags as `build-linux.sh` (`scons platform=linuxbsd target=editor accesskit=yes webgpu=yes
  gdscript_braces=yes linker=mold`), ccache via `PATH="$HOME/.cache/godotwebgpu-ccache-bin:$PATH"` and
  `CCACHE_BASEDIR=<your worktree>` (so objects are shared between worktrees), under the `build` lease. Web template:
  `source ~/emsdk/emsdk_env.sh; scons platform=web target=template_debug dlink_enabled=no webgpu=yes opengl3=no threads=no
  gdscript_braces=yes module_raycast_enabled=yes`. `pgrep -ax scons` first in your own tree.
- **Verification pattern that worked for grain and LUT**: a scratch Godot project in your scratch directory built from
  a `-s script.gd` SceneTree script, rendered with `xvfb-run -a -s "-screen 0 2600x1500x24" <editor> --path <proj>
  --rendering-method forward_plus|mobile|gl_compatibility --resolution WxH -s script.gd -- args`, saving
  `get_viewport().get_texture().get_image()` after ~10 frames, then numbers checked in Python against an independent
  reference (numpy, or colour-science in a venv) — not only "it changed". For the web: export with a `custom_template/debug`
  preset (headless export), serve, and capture the canvas with Playwright using **system Chrome headed**:
  `executablePath: '/usr/bin/google-chrome-stable', headless: false, args: ['--enable-unsafe-webgpu',
  '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'], ignoreDefaultArgs: ['--enable-unsafe-swiftshader']`,
  check the adapter is nvidia/lovelace with maxSampledTexturesPerShaderStage 48, and wait for the `#status` overlay to
  hide before capturing (an unbaked export compiles shaders under it). Compare with the native Forward+ render.
  Playwright is in `webgpu_tests/forward_plus/node_modules`. Take the `gpu` lease for rendering runs.
- **Docs**: class reference XML for everything you add (check with `--doctool .` and restore unrelated files it
  touches; never `--no-docbase`). Put text for TASKS.md §4 and README in `results/<id>/for-tasks.md`, and **user-facing
  wiki text** (what it does, how to use it, an example, limits per renderer) in `results/<id>/for-docs.md`: the lead
  adds a wiki page per feature group under Engine Additions.
- **Questions for the user** go in `results/<id>/for-tasks.md` under "Questions for the user" with a default; build with
  the default meanwhile.
- Commits: plain messages, no `Co-Authored-By` or session trailers. Never push.
