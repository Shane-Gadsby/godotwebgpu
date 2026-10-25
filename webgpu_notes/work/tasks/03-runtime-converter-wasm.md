# 03-runtime-converter-wasm: the engine converts with SPIRV-Cross at runtime and in the export-time baker

State: merged e05c777b8d; same build + smoke; real-GPU boot of an unbaked export pending (lead, after the parity run)
Owner: worker-03
Claimed:
Needs: build (web templates + a native editor)
Branch: work/3
Results: webgpu_notes/work/results/03-runtime-converter-wasm/

## Context

`webgpu::spirv_to_wgsl()` (`drivers/webgpu/spirv_to_wgsl.cpp`) is the in-engine converter: the runtime fallback for
shaders without baked WGSL (web templates) and the in-process path where the baker uses it. It calls
`tint_wrapper_spirv_to_wgsl()`; `drivers/webgpu/SCsub` compiles ~384 Tint TUs into every web variant, plus
`tint_wrapper.cpp` / `tint_ir_transforms.cpp`, and `_pipeline_fingerprint()` hashes Tint's sources so a Tint change
re-bakes `wgsl_precompiled.gen.h`.

## Goal

1. SCsub: compile `thirdparty/spirv-cross-wgsl` (same 7 TUs as `tint_cli/build.sh`, `SPIRV_CROSS_NAMESPACE_OVERRIDE=
   spirv_cross_wgsl`, its own include dir first) and `spirv_cross_wrapper.cpp`; `spirv_to_wgsl()` calls it. Keep Tint
   selectable with a temporary SCons option (e.g. `webgpu_wgsl_backend=tint`) until task 09, default SPIRV-Cross.
2. **Exceptions in wasm.** SPIRV-Cross throws on unsupported input; Godot builds `-fno-exceptions` and an uncaught throw
   in wasm aborts the engine. Decide and implement: Emscripten JS exceptions vs `-fwasm-exceptions` (browser support:
   our targets are current Chrome/Firefox/Safari) vs something else, scoped to the SPIRV-Cross TUs + wrapper if the link
   allows. Measure the wasm size and a frame-time sanity check of whichever you pick; dlink (`dlink_enabled=yes`, side
   modules) and `threads=yes` variants must still link and boot.
3. `_pipeline_fingerprint()` hashes `thirdparty/spirv-cross-wgsl` and the wrapper instead of Tint when that backend is
   on; the precompiler already uses `bin/tint_convert_cli`.
4. Measure: release nothreads wasm size (uncompressed + gzip/brotli) Tint vs SPIRV-Cross, and the web template build time.

## Verification

All 8 web variants of `build-linux.sh` link (or the 4 the lead names, if time is short) and the non-dlink nothreads
template boots a scene smoketest export headed on the real GPU (`gpu` lease) with `translated > 0` coming from
SPIRV-Cross (an unbaked export forces runtime conversion). Sizes and timings in SUMMARY.md.
