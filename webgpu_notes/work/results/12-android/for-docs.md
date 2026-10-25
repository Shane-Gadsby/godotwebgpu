# For the shared docs (task 12)

## thirdparty/README.md, spirv-cross-wgsl "Patches" list (after 0015)

- `0019-no-for-statements.patch` — no WGSL `for` statement is ever emitted (`ControlFlowSyntax::support_for_loop`,
  false for WGSL): both `ForLoop` cases of `attempt_emit_loop_header()` fall back to the generic `loop { if (!c) { break; } ... }`
  form, the shape Tint always emitted. On the Razr 60 (Mali-G615, Chrome 155) a counted loop starting below zero,
  `for (var i = -3; i <= 3; i++)` in volumetric fog's filter pass, ran zero times and the fog lost its density
  (12.6 mean abs diff from native Vulkan; the same loop as `loop`/`while`, or counted from 0, renders correctly).
