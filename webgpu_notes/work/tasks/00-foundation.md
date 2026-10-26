# 00-foundation: vendor spirv-cross-wgsl, a converter wrapper, tint_convert_cli on SPIRV-Cross

State: integrated
Owner: lead
Claimed: 2026-10-25 09:25
Needs: —
Branch: (lead, on webgpu-4.8)
Results: webgpu_notes/work/results/00-foundation/

## What was done

- `thirdparty/spirv-cross-wgsl/`: Ludicon/spirv-cross-wgsl at `bad70810418b10064d9ea91e51c78935f112ba5c` (branch `wgsl`):
  core + GLSL + WGSL backends only, its own `spirv.hpp` and `NonSemanticShaderDebugInfo100.h` (newer than
  `thirdparty/spirv-headers`). Separate from `thirdparty/spirv-cross` (Metal's) and compiled in its own namespace
  (`SPIRV_CROSS_NAMESPACE_OVERRIDE=spirv_cross_wgsl`) so a macOS editor linking both has no duplicate symbols and Metal
  is untouched. Built with `-fexceptions` (SPIRV-Cross reports errors by throwing).
- `drivers/webgpu/spirv_cross_wrapper.{h,cpp}`: same C-style contract as `tint_wrapper` (malloc'd WGSL or error), never
  aborts, demotes vertex-stage storage buffers to `read` (Tint's `DemoteVertexStageReadWriteStorage`).
- `tint_cli/build.sh`: `WGSL_BACKEND=spirv-cross` (default) | `tint`; `OUTPUT=` override. `main.cpp` picks the wrapper at
  compile time (`WGSL_BACKEND_TINT`): a run-time switch would share the export bake cache, which is keyed by the
  converter binary's own hash, between two backends.
- `webgpu_notes/work/` board, `lease.sh`, `smoke.sh`.

## Verified

`smoke.sh` on the lead tree, 2026-10-25 09:38: 276/276 converted by both, interface identical 276/276, Chrome 275/276
valid (FSR2 luminance pyramid: task 01). The new `bin/tint_convert_cli` is 8.8 MB (Tint build: 15.5 MB).
