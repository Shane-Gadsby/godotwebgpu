# 02: every place the driver reads WGSL text accepts SPIRV-Cross's

## What changed

New `drivers/webgpu/wgsl_text_scan.{h,cpp}` (std only, no engine types) holds every WGSL text read the driver made,
as a structural parser of module-scope declarations (attributes in any order, any whitespace/comments, `u`/`i`
integer suffixes, `var<space, access>`) instead of `strstr` over `@group(`. `rendering_device_driver_webgpu.cpp` and
the baker's `_wgsl_declares_override` (rendering_shader_container_webgpu.cpp) call it; ~1,500 lines of ad-hoc scanning
left the driver. The same unit is compiled natively by `webgpu_tests/wgsl_text_scan/run_tests.sh`.

Why a shared unit and not a JS port: the scans are about 1,700 lines of text rewriting; a port would be a second
implementation to keep faithful. The unit is the real code, so the test cannot drift from the driver. It is std-only
(`std::string`, not `String`), the one deviation from the engine's container rule, for the same reason `tint_cli` is
exempt: it has to compile outside the engine. Behavior for Tint output is unchanged by construction (the old logic was
moved, not redesigned; the reclassifier is the old function verbatim apart from types).

## Inventory

JS side (`platform/web/js`, no WGSL text handling anywhere; `engine.js` only mentions it in comments),
`spirv_lite_reflect.*` and `wgsl_bake_subprocess.*` (SPIR-V reads and subprocess plumbing, no WGSL parsing): nothing to do.

| Consumer (old location) | What it extracts | Failure mode if it misses | Tint | SPIRV-Cross before | SPIRV-Cross after |
|---|---|---|---|---|---|
| `parse_group_binding` + the `@group(` loops (all below) | group/binding of each resource | binding invisible to every map below | `@group(0u) @binding(1u)` | accepted (`u` optional) | accepted; any order/whitespace |
| texture dimension / multisampled / depth / sample type (~5816) | `wgsl_tex_dims`, `_is_multisampled_texture`, `_is_depth_texture`, `_tex_sample_type` | wrong `viewDimension`/`sampleType` in the BGL: Dawn validation error or blank texture | ok | ok | ok, identical on 276/276 |
| comparison samplers | `wgsl_is_comparison_sampler` | wrong sampler binding type | ok | ok | ok |
| `var NAME :` to key map + `textureSample*(tex, samp` call scan (~5963) | which sampler is used with a depth texture (NonFiltering, shared-sampler check) | Filtering sampler with a Depth texture: Dawn error at pipeline creation | ok | ok (names are `_NNN`, still plain identifiers) | ok; now pairs resolved from this stage's own vars |
| buffer access (~6037) | `wgsl_ssbo_readonly`, `wgsl_is_uniform` | writable SSBO treated read-only or reverse | `var<storage>` = read-only | `var<storage, read>` | both parse to the same; 471 SSBO vars identical |
| storage texture format + access (~6082) | `wgsl_storage_tex_format/_access` | wrong format silently defaults to RGBA8Unorm | ok | ok | ok |
| override ids (~6226) and the baker's `_wgsl_declares_override` | `@id(N) override NAME` used in body | override not set (shader runs on its default) or Safari crash for unused ones | `@id(1)` | `@id(1)` | ok, 216 override decls identical; `@id( 1 )`, comments tolerated |
| read_write split (~5353, legacy ~10916) | `texture_storage_*<fmt, read_write>` to write + `_rw_in` sampled shadow | Safari/Firefox reject the BGL | ok | ok | ok, 15 splits identical |
| read-only storage to sampled (~5569) | `<fmt, read>` to `texture_*<T>` plus the `textureLoad` mip argument | Firefox rejects the BGL | ok | ok | ok, 9 conversions identical; call sites matched with whitespace/newlines now |
| single-component depth reclassification (~4496, legacy ~11121) | `texture_2d<f32>` with "depth" in the name read only through `.x`/`.r` to `texture_depth_2d` | Depth texture bound to a Float slot: replaced by a blank fallback, silently | 22 reclassified | **21 of 22 (differs)** | **22 of 22 (fixed)** |

The one real difference: `screen_space_reflection_downsample.glsl_default_comp`. SPIRV-Cross writes
`let _27 = textureLoad(source_depth, p, 0);` and swizzles at each use (`_27.x`), where Tint put `.x` on the call. The
reclassifier only knew the Tint shape, so `source_depth` stayed `texture_2d<f32>` and would have been served the blank
depth fallback. It now follows a `let _<digits> = <call>;` whose every other use is `.x`/`.r` and drops those swizzles
(SPIRV-Cross ids are unique module-wide, which is what makes "every use of the name" mean this value; other names are
not followed).

## Verification

- `webgpu_tests/wgsl_text_scan/run_tests.sh <smoke run dir>`: runs the unit over `ref/` (Tint) and `wgsl/` (SPIRV-Cross) of all
  276 variants, in both the "no read_write storage textures" and "keep r32 read_write" device modes, and requires identical
  output (bindings, dims, depth, sample types, formats, access, buffer access, pairs, overrides, splits, conversions,
  reclassified set). Before the let-following fix: 1 of 276 differs (above). After: 0 of 276 in both modes
  (1904 resource vars, 668 sampler pairs, 216 overrides, 22 reclassifications, 15 splits, 9 conversions: not a vacuous pass).
- 4 hand-written cases (`cases/*.wgsl` + `.expected`): attribute spellings (suffixes, reversed order, multiline), storage
  rewrites and overrides, the forwarded-let depth shape, and its negative (the vec4 is also used whole, so not reclassified).
- `VALIDATE=1 run_tests.sh <dir>`: the rewritten SPIRV-Cross text of all 276 variants compiles in Chrome (0 invalid beyond the known list).
- `smoke.sh`, last lines:

```
adapter: {"vendor":"nvidia","architecture":"lovelace","fallback":false,"features":["shader-f16","subgroups","clip-distances","dual-source-blending"]}
/tmp/claude-1000/w02/smoke2/smoke-20261025-095724-34466/wgsl: 276 files, 1 invalid
validation: 276 modules, 1 invalid, 1 of them expected
SMOKE OK (smoke-20261025-095724-34466)
```

## Not done: the web template compile check

The `build` lease was held by worker-03 (running a template matrix) for the whole session, polled for ~40 minutes. The
driver edits are therefore **not compiled for web**: `rendering_device_driver_webgpu.cpp` needs
`scons platform=web target=template_release dlink_enabled=no webgpu=yes opengl3=no threads=no` (capped scope, per the board rule)
and the native editor with `webgpu=yes` for `rendering_shader_container_webgpu.cpp`'s baker path. `wgsl_text_scan.cpp`
itself compiles clean with `-Wall -Wextra` natively, and pre-commit (clang-format etc.) passes. Likely failure spots if any:
`WEBGPU_DIAG` and `String(const char*)` call sites in the rewritten rw-split / read-convert blocks.

## Risks

- Unit is std-only (see for-tasks.md question). Web size impact is small.
- Behaviour for Tint output was moved, not re-derived; the corpus test compares Tint to SPIRV-Cross, not new code to the old
  driver code, so a transcription slip that affects both backends equally would not show. Task 07's parity run covers that.
