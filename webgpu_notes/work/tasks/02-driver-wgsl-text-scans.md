# 02-driver-wgsl-text-scans: every place the driver reads WGSL text must accept SPIRV-Cross's

State: integrated (unbaked parity 71/71 on the 62a570d31f pair; SSR reflections back)
Owner: worker-02
Claimed: 2026-10-25 09:45
Needs: build (one web template compile check at the end)
Branch: work/2
Results: webgpu_notes/work/results/02-driver-wgsl-text-scans/

## Context

The driver derives things from the WGSL text after conversion (bind-group layout entries, texture sample types,
storage-texture formats and access, override ids, depth textures, ...): `strstr("@group(")`, `"@id("`, `"override"`
scans in `drivers/webgpu/rendering_device_driver_webgpu.cpp` (~lines 89-140, 4453, 5363, 5579, 5817, 6043, 6083, 6229,
10927 at `pre-SPIR-V-Cross`) and `rendering_shader_container_webgpu.cpp` (~101), plus anything in
`spirv_lite_reflect.*`, `wgsl_bake_subprocess.*` and the JS side. They were written against Tint's formatting, e.g.
`@group(0u) @binding(1u)` (SPIRV-Cross writes `@group(0)`), "Tint format: `@group(G) @binding(B) var NAME: texture_...`",
Tint's names for things, the order of declarations, `diagnostic(...)` / `enable` lines, struct layout spelling.
A scan that silently finds nothing degrades to a fallback (a blank texture, a wrong sample type, a missing override)
instead of failing — exactly the class of bug the parity matrix then has to find by eye.

## Goal

1. Inventory every WGSL text consumer (C++ and JS) with what it extracts and how it fails.
2. A test that runs each extraction over both backends' WGSL for the whole engine corpus (`smoke.sh` leaves
   `<scratch>/<run>/ref` = Tint and `<run>/wgsl` = SPIRV-Cross) and asserts identical results — ideally by moving
   the parsers into a small standalone-compilable unit the test and the driver share (as `spirv_lite_reflect` is), or
   with a faithful port in `webgpu_tests/driver_unit_tests` if not.
3. Fix every scan that differs so it accepts both formats (it must keep working for Tint until task 09 removes it).

## Verification

The new test passes over all 276 variants for both backends; SUMMARY lists each consumer and its before/after result
counts. A web template compiles (`build` lease; `scons platform=web target=template_release dlink_enabled=no webgpu=yes
opengl3=no threads=no` in your worktree). The lead's Forward+ parity run (task 07) is the end-to-end check.
