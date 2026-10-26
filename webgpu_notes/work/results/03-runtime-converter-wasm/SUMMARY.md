# 03-runtime-converter-wasm: summary

State: ready-for-integration (branch work/3). The engine's runtime converter (`webgpu::spirv_to_wgsl`) and the web
templates now use SPIRV-Cross by default; Tint stays selectable with `webgpu_wgsl_backend=tint` until task 09.

## What changed
- `platform/web/detect.py`: new option `webgpu_wgsl_backend` (spirv-cross default | tint); with spirv-cross it adds
  `-fwasm-exceptions` to LINKFLAGS.
- `drivers/webgpu/SCsub`: Tint's TU block only builds for `tint` (define `WGSL_BACKEND_TINT` for the driver); the
  spirv-cross branch compiles the 7 `thirdparty/spirv-cross-wgsl` TUs + `spirv_cross_wrapper.cpp` with
  `SPIRV_CROSS_NAMESPACE_OVERRIDE=spirv_cross_wgsl`, the vendored dir first on CPPPATH, `-fno-exceptions` removed and
  `-fwasm-exceptions` added (only those TUs). `_pipeline_fingerprint()` now starts with the backend name and hashes
  `thirdparty/spirv-cross-wgsl` or `thirdparty/tint` (only the selected one), plus `spirv_cross_wrapper.cpp` in the
  dependency list. The precompiler still uses whichever `bin/tint_convert_cli` exists (its hash is in the fingerprint,
  so switching backend + rebuilding the CLI with `WGSL_BACKEND=tint` re-bakes).
- `spirv_to_wgsl.cpp`, `rendering_device_driver_webgpu.cpp`: Tint calls/includes behind `WGSL_BACKEND_TINT`; the
  error message names the backend.
- `spirv_cross_wrapper.cpp`: added `catch (...)` after the `std::exception` catch.

## Design point 1: exceptions in wasm (decision: -fwasm-exceptions, scoped)
Standalone test (`em++ -O2`, the 7 TUs + wrapper + a main feeding a module with no entry point and one with a bad
magic; Emscripten 6.0.9, node):
| mode | wasm bytes | result |
|---|---|---|
| -fwasm-exceptions | 948,890 | both inputs return nullptr + message, process survives |
| -fexceptions (JS) | 1,243,721 | same, 31% bigger, every may-throw call goes through JS invoke_* |
| -fno-exceptions | n/a | does not compile (SPIRV-Cross uses try/catch) |
In the engine: only the SPIRV-Cross TUs get the flag, the link gets `-fwasm-exceptions` (needs the legacy-except libc++,
which Emscripten generated on first link). The rest stays `-fno-exceptions`. Proof it works in the real binary:
the software-adapter boot threw "Storage image format is not supported in WGSL" 28 times (one converter limitation,
see below); each was caught, the driver logged "conversion failed" and fell back, and the scene reported PASS.
Side modules/dlink and pthreads: all four variants link and boot (below). Browser floor: wasm EH (legacy) needs
Chrome 95+/Firefox 100+/Safari 15.2+. Frame-time sanity: not measured (the throw path only runs at shader
conversion, never per frame; a real-GPU timing was left because the GPU was reserved).

## Design point 3 / fingerprint: done as above.

## Measurements (release, nothreads, non-dlink; Emscripten 6.0.9, -j12, ccache warm)
| | raw | gzip -9 |
|---|---|---|
| SPIRV-Cross | 42,925,644 | 11,962,537 |
| Tint (webgpu_wgsl_backend=tint) | 43,614,945 | 12,204,107 |
| delta | -689,301 (-1.6%) | -241,570 (-2.0%) |
brotli is not installed here; not measured. Tint saves less than expected because the SPIRV-Tools optimizer
(needed by the preprocessing passes) stays linked for both. The 51.7 MB wasm in the lead's bin/ is older and not a
fair baseline.
Build time: SPIRV-Cross 102 s for a no-change relink after an object build (full first build of the tree incl. 7
SPIRV-Cross TUs: 368 s, cold-ish); Tint switch: 121 s because 366 Tint TUs came out of ccache. A cold Tint compile was not
measured (ccache hid it); the 7 SPIRV-Cross TUs vs 366 Tint TUs is the structural saving.
All 8 variants (release+debug x nothreads/threads x dlink on/off) built in 324-483 s each, all linked:
| variant | main wasm raw | side module raw |
|---|---|---|
| release nothreads | 42,925,644 | |
| release threads | 42,702,229 | |
| release nothreads dlink | 1,402,455 | 47,796,099 |
| release threads dlink | 1,441,276 | see bin/ |
| debug nothreads | 43,335,901 | |
| debug threads | 43,749,909 | |
| debug nothreads dlink | 1,461,980 | |
| debug threads dlink | 1,504,161 | |

## Boot checks (software adapter only: swiftshader, headless, Forward Mobile; no real-GPU render, GPU was reserved)
Unbaked export of webgpu_tests/test_project (`boot_check.mjs`, shader baker off) with the lead's existing editor binary
used read-only as an export tool:
- nothreads non-dlink: PASS, shaderStats baked=0 precompiled=85 cached=156 translated=502 (runtime conversion by
  SPIRV-Cross), 299 s.
- nothreads dlink: PASS, same counts, 177 s. threads dlink: PASS, same counts, 283 s, no page errors.
The lead still has to run the real-GPU Forward+ render check.

## smoke.sh (run in this worktree)
```
WGSL bytes: reference 6287157, candidate 6686228 (1.063x)
== Chrome validation
adapter: {"vendor":"nvidia","architecture":"lovelace","fallback":false,"features":["shader-f16","subgroups","clip-distances","dual-source-blending"]}
/tmp/claude-1000/w03-smoke/smoke-20261025-103308-3370579/wgsl: 276 files, 1 invalid
validation: 276 modules, 1 invalid, 1 of them expected
SMOKE OK (smoke-20261025-103308-3370579)
```

## Known problems found (not this task's)
- 28 console errors per boot: "SPIRV-Cross: Storage image format is not supported in WGSL." for stage 4 shaders
  (shader_create_from_container fails -> RID()). This is a converter gap for the converter-patch tasks (a storage image whose format SPIRV-Cross cannot map); smoke.sh does not catch it.
  Which shaders: re-run boot_check.mjs with VERBOSE=1.
- `webgpu_tests/scene_smoketest/node_modules` is untracked and absent in fresh worktrees; smoke.sh needs it (I symlinked
  the lead's; not committed).
