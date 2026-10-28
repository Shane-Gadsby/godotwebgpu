# 58-small-web-correctness: TASKS §2.42, §2.43, §2.44

State: ready-for-integration
Owner: worker-58
Claimed: 2026-10-28
Needs: build, gpu (short runs)
Branch: work/58
Results: webgpu_notes/work/results/58-small-web-correctness/

User request (2026-10-28): begin the remaining tasks. Three small, independent items from TASKS §2, all left over from
the SPIRV-Cross swap (§4.36). Do them in this order; each gets its own commit and its own section in SUMMARY.md.

1. **§2.44** `run_spec_ops.sh`'s `op_FConvert_f32` probe is invalid SPIR-V (`spec_op_probes.py` builds an FConvert to
   the same width). Confirm with `spirv-val --target-env vulkan1.2`, then fix the probe so it is a valid FConvert
   (f32 <-> f16 or f64, if the converter path still exercises what the probe was for) or drop it with the reason. Run
   `run_spec_ops.sh` once and record the result.
2. **§2.43** Audit swizzled texture views. WebGPU views have no component swizzle and the driver stores one without
   applying it, silently (§3 trap 55: FSR2's reactive mask). Find every non-identity swizzle that can reach
   `RenderingDeviceDriverWebGPU` (`texture_storage.cpp` image formats L8/LA8/R8-as-alpha etc., `alpha_only_view`,
   anything else: grep `swizzle_` across servers/ and modules/). For each: convert the data up front, swizzle in the
   shader, or prove it never reaches the web. Then make the driver **warn once per texture format/swizzle** when it
   still drops one (`WARN_PRINT_ONCE`-style; pick the severity per CLAUDE.md's web logging note). Test with an L8 and
   an LA8 `Image` texture on a material (native vs web).
3. **§2.42** FSR2's accumulate pass and the Forward+ scene shader's set 1 binding 24 get the blank depth fallback
   (`--verbose` log). Check FSR2 with the camera **moving** and the sky in view (native vs WebGPU); if it differs, make
   the binding a depth texture by name or teach `_reclassify_single_component_depth_textures()` its use, then check
   again. Trace binding 24's 4x4 depth placeholder (§2.31) and say what it is.

Update the three §2 entries' text in for-tasks.md (what you found, done or not); the lead moves them to §4.
