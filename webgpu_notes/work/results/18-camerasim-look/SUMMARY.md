# Task 18: cameraSim by eye, native Vulkan beside WebGPU (SPIRV-Cross), 2026-10-25

**Result: no visible difference in any of the 5 views.** WebGPU (SPIRV-Cross baked) matches native Vulkan almost pixel
for pixel: mean absolute difference 0.00 to 0.03 (of 255), at most 0.08% of pixels differ by more than 4. A Tint-baked
export differs from native by the same amount, and two runs of the *same* SPIRV-Cross export differ from each other by as
much (up to 0.03% of pixels > 4 in the lights view), so what is left is run-to-run noise, not the converter.
Console clean in both web exports (0 errors, 0 warnings), every stage baked (`baked 1570, translated 0, specialized 0`).

## Pairs (native left, WebGPU SPIRV-Cross right), `looks/`

| view | what it shows | MAD (0-255) | MAD 3D area | px > 4 | max | Tint-baked vs native: MAD / px > 4 |
|---|---|---|---|---|---|---|
| `default.png` | the scene as it loads: 50mm, f/8, full body, sky, floor, glow, TAA | 0.00 | 0.00 | 0.000% | 1 | 0.00 / 0.000% |
| `face_200mm.png` | 200mm button at face height: skin, eyes, hair strands + shell, eyebrows/lashes | 0.01 | 0.01 | 0.028% | 24 | 0.01 / 0.028% |
| `dof_f1_4.png` | aperture slider at its minimum (strong near+far DOF blur), shutter 1/4000 | 0.03 | 0.03 | 0.083% | 50 | 0.02 / 0.026% |
| `hair_side_200mm.png` | camera orbited 50 degrees to the side, 200mm at face height: hair from the side | 0.01 | 0.02 | 0.029% | 30 | 0.01 / 0.013% |
| `face_200mm_lights.png` | the key/fill/rim spots switched on (spot shadows, soft filter quality 3), shutter 1/500 | 0.01 | 0.01 | 0.021% | 22 | 0.01 / 0.023% |

Same-export repeat (SPIRV-Cross run 1 vs run 2), px > 4: 0.000 / 0.000 / 0.000 / 0.000 / 0.032%. Tint vs SPIRV-Cross:
0.000 / 0.000 / 0.066 / 0.029 / 0.030%. The few differing pixels are scattered over the character (hair and body
outline), not a region or a material. `looks/tint/` has the same five pairs with the Tint-baked export on the right.
"MAD 3D area" leaves out the left camera panel (x < 22%). The FPS counter label is hidden in every capture (it differs by
design).

The last two views are not reachable from the app's UI: there is no camera-angle slider, and the scene ships its three
spot lights hidden (`visible = false`; nothing in the scripts shows them). They were added for coverage (hair from the
side, positional shadows); drop them from the page if only UI-reachable views count.

## How the pairs were made and aligned

- Scratch copy of the project (the original was not touched), plus an autoload `t18_capture.gd` (this folder) that steps
  through the views. Native and web run the same script: each view is applied through `main.gd`'s own functions and
  the UI's sliders, held for a fixed number of frames (360 for the first, 240 for each next), then the root viewport is
  read back with `get_image()` (WebGPU readbacks are asynchronous, so it retries on the next frames: 3-4 tries) and
  saved. Both sides capture at the same frame numbers (native 361/602/843/1084/1325, web 363-364/606-607/849-850/
  1092-1093/1335-1336). Both cap at 60 fps; native uses `--fixed-fps 60` so each frame's delta is 1/60 s, the web runs at
  a measured 60.0 rAF/s, so the hair simulation and TAA see the same time steps. 1920x1080 on both sides.
- Native: the lead's `bin/godot.linuxbsd.editor.x86_64` (14:17) running the project, `--rendering-driver vulkan`, real
  display: `Vulkan 1.4.351 - Forward+ - NVIDIA GeForce RTX 4080 SUPER`.
- Web: the lead's editor + `godot.web.template_release.wasm32.nothreads.zip` (14:18, same build; the export's wasm md5
  matches the template's), baked with `bin/tint_convert_cli` (SPIRV-Cross) or `bin/tint_convert_cli.tint-pre-swap`
  (Tint), `WEBGPU_BAKE_CACHE=0`. Headed system Chrome with the Vulkan flags; adapter `nvidia / lovelace`,
  `isFallbackAdapter=false`, `maxSampledTexturesPerShaderStage=48`, engine banner `WebGPU 1.0 - Forward+`.
- **One preset change** (see Questions): `vram_texture_compression/for_desktop=true` (Godot's default) instead of the
  user's `false`; everything else is the user's own preset (variant pruning on, `shader_baker/separate_pack=false`).
- Scripts: `export.sh` (export with a given converter), `capture_native.sh`, `capture_web.mjs` (fails on a fallback
  adapter or a missing view), `pair.py` (side-by-sides and the numbers). Per-run logs and stats in `runs/`.

## Load time (headed Chrome, 4080 SUPER, served from localhost; two runs each, alternating order)

| export | main scene ready | first capture (frame 360) | shaders | pck |
|---|---|---|---|---|
| SPIRV-Cross | 3.73 s / 2.70 s | 18.1 s / 14.3 s | baked 1570, translated 0 | 241.7 MB |
| Tint-baked | 3.02 s / 2.59 s | 19.6 s / 16.3 s | baked 1570, translated 0 | smaller shader section (as in task 08) |

"First capture" is 360 frames after the scene starts, so it includes the browser's pipeline compiles (360 frames take
6 s at 60 fps without stalls); no difference between converters beyond the run-to-run spread.

## Not the swap, but seen

- **The user's own preset ships Ava without textures.** With `vram_texture_compression/for_desktop=false` and
  `for_mobile=false` (the user's settings), every VRAM-compressed texture of Ava fails to load on the web:
  `No loader found for resource: (expected type: CompressedTexture2D)` / `Failed loading resource:
  res://character/Ava/Maps/Body_Basecolor.png` and the rest of the Body, Head, Teeth, Eyes and Clothes maps, 132 console
  error lines. The user's last export (`builds/`, 2026-10-21 11:26) predates Ava (`model_ava.tscn`, 14:53 that day), so
  they may not have seen it. Same with either converter; not a shader issue.
- Two app-script quirks, identical on both backends: `exposure_controller.gd` declares `var aperture := 8` (an int), so the
  f/1.4 slider minimum becomes f/1 (label "f/1.0", EV -6 at the default shutter: a white frame); and
  `main.update_camera_position()` aims the camera correctly only at `camera_angle = 90` (it sets yaw `angle - 90`, it
  needs `90 - angle`). Neither is a rendering difference.
- With the spots on, the lit skin has a greenish cast, on native too (the project's own lighting/materials).

## Questions for the user

1. **Texture compression in the cameraSim web preset** (choice): the preset has both VRAM compression options off, which
   exports Ava untextured. (a) turn `vram_texture_compression/for_desktop` on (Godot's default; what these pairs used),
   (b) turn both on (separate texture packs, the shell fetches only the one the device samples; needed for Android),
   (c) leave as is. Suggested default: (b) if the project should run on phones, else (a).
2. **Views**: are the two views not reachable from the UI (side orbit, spot lights on) wanted on the page, or only the
   three UI ones?

## For the lead

- Post `looks/*.png` (5 pairs, 1600 px wide) on the hand-offs page; `looks/tint/` if the Tint comparison is wanted.
- No code changed; smoke.sh not run (not needed for this task). Nothing to integrate except this folder.
- To redo after a SPIRV-Cross patch: copy the project, append `[autoload]` / `T18Capture="*res://t18_capture.gd"` to its
  project.godot and copy the script in, `export.sh sc <proj> <scratch>/web-sc bin/tint_convert_cli`,
  `capture_native.sh <proj> <scratch>/native` and `node capture_web.mjs <scratch>/web-sc <scratch>/cap-sc` under the gpu
  lease (about 3 minutes), then `pair.py <scratch>/native <scratch>/cap-sc <out> SPIRV-Cross`.
- Risk: one GPU, one browser; the look on Firefox/Android is tasks 11/12's.
