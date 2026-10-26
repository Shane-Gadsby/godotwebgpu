# For the shared docs (lead copies in)

CLAUDE.md, `drivers/webgpu/` file roles table, one new row:

| `wgsl_text_scan.{h,cpp}` | Everything the driver reads out of converted WGSL text (bind group layout facts: texture dimension/sample type/depth/multisampled, comparison samplers, storage-texture format and access, buffer access, override ids, texture/sampler pairs) and the three text rewrites (read_write storage split, read-only storage to sampled, single-component depth reclassification). std only, no engine types, so `webgpu_tests/wgsl_text_scan/run_tests.sh` runs the same code natively over Tint's and SPIRV-Cross's output. Reads module-scope declarations structurally (any attribute order, whitespace, `u`/`i` suffixes), never with `strstr` |

CLAUDE.md "Testing" block, one new line:

```bash
# The driver's WGSL text reads over both converters' output for every engine variant (needs a smoke.sh run dir, which has ref/ and wgsl/):
webgpu_tests/wgsl_text_scan/run_tests.sh <smoke run dir>      # VALIDATE=1 also compiles the rewritten text in Chrome
```

Rule worth a CLAUDE.md line: new code that reads converted WGSL goes through `wgsl_text_scan` (and gets a case in
`webgpu_tests/wgsl_text_scan/cases/`), not `strstr`. A scan that finds nothing does not fail; the binding silently gets a default.
