# Task 09 for the shared docs (lead edits these)

Tint is gone from the tree on work/9. Everything below is what the shared docs still say about it. General rule for
all of them: `tint_convert_cli` -> `wgsl_convert_cli`, `drivers/webgpu/tint_cli/` -> `drivers/webgpu/wgsl_cli/`,
`TINT_CONVERT_CLI` -> `WGSL_CONVERT_CLI`, `TINT_DEBUG_DUMP_PREPROCESSED` -> `WGSL_DEBUG_DUMP_PREPROCESSED` (the old
name still works for one release), `--skip-tint-cli` -> `--skip-wgsl-cli` (old flag still accepted), "Tint" as our
converter -> "SPIRV-Cross" (or "the converter"). Mentions of the browser's own Tint (Chrome/Dawn compiling WGSL,
trace events named Tint) stay. Pass names `lower_tint_unsupported_ops` and the fixture `tint_crash_inputs.frag` were
NOT renamed (see SUMMARY, question 1), so references to them stay as they are.

## CLAUDE.md

- l.31-35 Build Commands: "`bin/wgsl_convert_cli` — the standalone host tool that does build-time SPIR-V→WGSL
  precompilation (our passes + SPIRV-Cross's WGSL backend) — is a **separate native build** ..."; commands
  `./drivers/webgpu/wgsl_cli/build.sh` / `--clean`.
- l.38: `bin/wgsl_convert_cli.exe`; "runs `wgsl_convert_cli` from its own executable's directory".
- l.40: "when iterating on `drivers/webgpu/spirv_preprocess.cpp`, `spirv_cross_wrapper.cpp`, or anything under
  `thirdparty/spirv-cross-wgsl`/`thirdparty/spirv-tools`"; "fingerprints the vendored SPIRV-Cross/SPIRV-Tools/`wgsl_cli`
  sources, `bin/wgsl_convert_cli` and glslang's version".
- l.42: "`wgsl_convert_cli --batch`"; "invalidated by a converter rebuild".
- l.46: build-linux.sh flag list: `--skip-wgsl-cli` (was `--skip-tint-cli`, still accepted).
- l.53: "just wgsl_convert_cli".
- l.114: expected_failures.json: "baseline of known/accepted conversion failures (Vulkan-only variants ...)".
- l.140: "(runtime, `wgsl_convert_cli`, fuzzer)".
- l.143: delete the `tint_wrapper.{h,cpp}` row; add
  `| spirv_cross_wrapper.{h,cpp} | Runs SPIRV-Cross's WGSL backend (thirdparty/spirv-cross-wgsl) on preprocessed SPIR-V; built with wasm exceptions, because SPIRV-Cross reports an unsupported shader by throwing |`
- l.145: `| wgsl_cli/ | wgsl_convert_cli — standalone native host tool wrapping spirv_preprocess + spirv_cross_wrapper for build-time precompilation, export-time baking and standalone testing/debugging (see Build Commands) |`
- l.152 pipeline diagram: `→ WGSL (SPIRV-Cross's WGSL backend, C++, linked directly into the engine — no WASM/JS translation step)`.
- l.156: "SPIRV-Cross's WGSL backend is vendored under `thirdparty/spirv-cross-wgsl` (Ludicon/spirv-cross-wgsl, local
  patches in its `patches/`, listed in `thirdparty/README.md`, never upstreamed); the passes need
  `thirdparty/spirv-tools` and `thirdparty/spirv-headers`." ... "the fix is almost always either a new/extended
  preprocessing pass here or a SPIRV-Cross patch".
- l.158 "Debugging a conversion failure": `wgsl_convert_cli`'s batch mode ... "so an abort in a pass or the
  converter doesn't kill the whole batch" ... "`WGSL_DEBUG_DUMP_PREPROCESSED=<path>` on `wgsl_convert_cli` dumps the
  SPIR-V after every pass but before SPIRV-Cross sees it (`TINT_DEBUG_DUMP_PREPROCESSED` still works for one release)".
- l.166: "normally left in the SPIR-V so SPIRV-Cross emits `@id(N) override` declarations".
- l.267: "the native `wgsl_cli` host tool is an exception".
- Testing: add a line under the shader corpus entry or near smoke: "`webgpu_notes/work/smoke.sh` (while the work board
  exists) and `webgpu_tests/wgsl_text_scan/run_tests.sh` compare against stored references made from Tint's output
  when SPIRV-Cross replaced it: `webgpu_tests/spirv_cross_eval/interface_reference.json` (bindings + override ids per
  engine variant) and `webgpu_tests/wgsl_text_scan/scan_reference.txt` (the driver's WGSL text-scan facts). After a
  deliberate interface change, regenerate them (`compare_interface.py --write`, `WRITE_REFERENCE=1 run_tests.sh`) and
  say why in the commit."

## README.md

- l.175 (known-issues row "`tint_convert_cli` is compiled without `-DNDEBUG`"): now `wgsl_convert_cli`; "SPIRV-Tools /
  SPIRV-Cross asserts are live in the host tool; they surface as isolated 'WGSL conversion crashed' bake entries"
  (that is the new error text from the batch child).
- l.185: "Fragile if the converter's output names change. Task 9.15 moved some of these patches to Tint IR
  transforms" -> those transforms are gone; the vertex-stage read_write demotion now happens in
  `spirv_cross_wrapper.cpp` (NonWritable on vertex-stage storage buffers).
- l.216: `| shader_corpus | 14 / 14 | wgsl_convert_cli only — no engine build |`
- l.451-456: `wgsl_convert_cli` / `wgsl_convert_cli.exe` beside the editor; `chmod +x godot.linuxbsd.editor.x86_64 wgsl_convert_cli`.
  Note for release notes: an editor built before this change looks for `tint_convert_cli`; editor and tool ship together.
- l.472-478: "# 3. wgsl_convert_cli — ...", `./drivers/webgpu/wgsl_cli/build.sh`.
- l.482: "No Rust toolchain needed (SPIRV-Cross's WGSL backend is compiled directly into the engine)".
- l.525: "SPIR-V → WGSL via SPIRV-Cross" (the figures were measured with Tint; keep or re-measure).

## drivers/webgpu/README.md

- l.18 diagram: "SPIR-V → WGSL translation (SPIRV-Cross, linked in)".
- l.38: "before conversion".
- l.39: "lowering of what Tint aborted on" -> "lowering of constructs WGSL has no direct form for".
- l.42: delete the `tint_wrapper.cpp/h` row; add `spirv_cross_wrapper.cpp/h` (~130 lines: SPIRV-Cross options, vertex
  read_write demotion, exceptions caught) and `wgsl_cli/` if it is listed.
- l.59-68: "GLSL → SPIR-V (glslang, at build time) → WGSL (SPIRV-Cross, linked in as C++). SPIR-V is preprocessed in C++
  before conversion ... SPIRV-Cross's TUs are built with `-fwasm-exceptions` (it throws on unsupported shaders) in their
  own environment, so the rest of the engine stays `-fno-exceptions`." Drop the C++20 isolation sentence.
- l.83-85: "**What WGSL has no form for** (`lower_tint_unsupported_ops()`, named for Tint, which aborted on them) ...
  rewritten into what the converter accepts."
- l.126-147: `wgsl_convert_cli --batch`; cache dir `v1-<sha256 of wgsl_convert_cli>`; "survives engine rebuilds and a
  converter rebuild invalidates it"; fingerprint "SPIRV-Cross/SPIRV-Tools/`wgsl_cli`, `bin/wgsl_convert_cli` itself";
  "3. **Runtime conversion** — anything that missed, translated in the browser ...".
- l.164, 187, 225, 240, 246, 250, 277-284: "Tint" -> "SPIRV-Cross" / "the converter" (WGSL baked from that SPIR-V;
  the runtime conversion; stages this driver converted; how much the converter had to do; `specialized` also converts
  at load time; an `OpSpecConstantOp` the override path cannot lower; for SPIRV-Cross to turn into `@id(N) override`;
  no runtime SPIR-V patching or re-conversion).
- l.262-268: `bin/wgsl_convert_cli` / `drivers/webgpu/wgsl_cli/build.sh` / `spirv_cross_wrapper.*` or
  `thirdparty/spirv-cross-wgsl`.
- l.392: "`tint_wrapper.cpp`'s `allow_non_uniform_subgroup_operations`" no longer exists. Since task 38 the passes lower
  subgroup ops anyway (`lower_subgroup_ops`), so: "the driver's own SPIR-V preprocessing lowers the built-in shaders'
  subgroup ops (`lower_subgroup_ops`, for Firefox) ...". Worth a check by whoever edits this section.
- The WGSL stats console line is now `[SHADER] WGSL stats: precompiled_hits=N cache_hits=N misses=N` (was
  `tint_misses=`; `webgpu_tests/wgsl_cache/verify_precompiled_hits.mjs` parses `misses=`, which never matched the old
  key).

## thirdparty/README.md

- Delete the whole `## tint` section (l.1210-1240 or so, with its patch list).
- l.1104 (spirv-cross-wgsl section): "compiled only into the WebGPU driver and `wgsl_convert_cli`".
- l.1122, 1129, 1133: "Tint does the same / Tint passed ... / the shape Tint always emitted" are comparisons with the
  former converter; fine as history, or "Tint (the converter before SPIRV-Cross) ...".
- l.1144 (spirv-headers): "required by spirv-tools" (drop "/Tint"). The broader `unified1` extraction is for
  SPIRV-Tools (DebugInfo, OpenCL, NonSemantic headers), not only Tint, so it stays; whether `spirv.hpp11` can return to
  the vulkan-sdk pin now that Tint is gone was not tested (SUMMARY).
- l.1191-1194 (spirv-tools): "Required by the WebGPU backend's SPIR-V preprocessing passes (`spirv_preprocess.cpp`,
  `spirv_opt_passes.cpp`: validation, the optimizer passes such as `CreateInlineOpaquePass()` ...). Originally vendored
  for Tint's SPIR-V reader. Version was pinned by Dawn DEPS; it now has no Dawn tie."

## webgpu_site/*

`git grep -n -i tint webgpu_site/` (ARCHITECTURE_AND_DESIGN 20, CORRECTNESS_AND_COMPATIBILITY 8, FAQ 11,
PERFORMANCE_AND_OPTIMIZATION 4, RAY_TRACING_DESIGN 2, TECHNICAL_REFERENCE 11 lines). Same rules; TECHNICAL_REFERENCE
§4.1's pass list keeps `lower_tint_unsupported_ops` by name. The FAQ's "why Tint" answer becomes "why SPIRV-Cross"
(TASKS §2.35's table has the numbers: 276/276 converted after the passes, interface identical, ~5-10x faster per
module, +6-9% WGSL bytes; parity 71/71).

## webgpu_tests/README.md

Done on the branch (tier table, pipeline diagram, troubleshooting entry), not lead-only.
