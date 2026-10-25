## 2.35 progress (task 03)
Runtime converter + web templates on SPIRV-Cross, all 8 variants link, 3 boot on software adapter with translated=502.
Wasm size -1.6% raw / -2.0% gzip vs Tint (SPIRV-Tools stays). Open: real-GPU check; "Storage image format is not
supported in WGSL" throws 28x at boot (converter patch needed).
## Questions for the user
None.
