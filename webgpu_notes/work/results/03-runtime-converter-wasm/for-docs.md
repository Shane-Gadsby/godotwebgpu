- drivers/webgpu/README.md / CLAUDE.md: new SCons option `webgpu_wgsl_backend=spirv-cross|tint` (default spirv-cross;
  temporary). SPIRV-Cross TUs are the only code built with `-fwasm-exceptions` (link gets the flag too); everything
  else stays -fno-exceptions. A throw inside the converter returns nullptr and the driver falls back (spirv_to_wgsl.cpp).
- _pipeline_fingerprint() hashes the selected backend's vendored tree plus the backend name.
