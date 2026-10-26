# For TASKS.md

§2.35: task 02 done. The driver's WGSL text scans moved into `drivers/webgpu/wgsl_text_scan.{h,cpp}` and run identically
over Tint and SPIRV-Cross output for all 276 engine variants (`webgpu_tests/wgsl_text_scan/run_tests.sh`). One real
difference found and fixed: SPIRV-Cross keeps a sampled texture's vec4 in `let _27 = textureLoad(depth_tex, ...);` and
swizzles at each use (`_27.x`), where Tint swizzles the call. The depth reclassifier only knew Tint's shape, so
`screen_space_reflection_downsample`'s `source_depth` would have stayed `texture_2d<f32>` and got the blank fallback
texture, silently. It now follows the forwarded let (SPIRV-Cross ids `_<digits>` only, since those are unique module-wide).

§3 trap candidate: "A string scan of generated WGSL that finds nothing does not fail". Cost: the depth case above is exactly
the kind the parity matrix finds by eye.

Not covered by the test (needs the lead's Forward+ parity run, task 07): that the *rewritten* text behaves right on a GPU.
The rewritten SPIRV-Cross text of all 276 variants does compile in Chrome (VALIDATE=1).

## Questions for the user

None blocking. One judgment call, default taken: `wgsl_text_scan` uses `std::string`/`std::vector` rather than the engine's
`String`/`Vector`, so the same code compiles in the native test and the driver (options: keep it std-only [taken], or
port to engine types and test through a shim like `spirv_lite_reflect`). Recommendation: keep.
