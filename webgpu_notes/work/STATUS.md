# Status (lead-maintained)

## 2026-10-25 09:40 — board opened

Foundation integrated (task 00, see its file): `thirdparty/spirv-cross-wgsl` vendored, `spirv_cross_wrapper`,
`tint_convert_cli` builds on SPIRV-Cross by default (`WGSL_BACKEND=tint` for the reference), `smoke.sh` OK on the lead
tree (276/276 converted, interface identical, 275/276 valid — FSR2, task 01).

| task | what | state | owner |
|---|---|---|---|
| 00 | foundation | integrated | lead |
| 01 | FSR2 barrier uniformity (SPIRV-Cross patch) | open | |
| 02 | driver WGSL text scans | open | |
| 03 | runtime converter in wasm, exceptions, SCsub | open | |
| 04 | spec constants | open | |
| 05 | real projects' shaders | open | |
| 06 | offline test tiers | open | |
| 07 | Forward+ parity vs Vulkan | open | lead (gpu) |
| 08 | GPU timing | open | |
| 09 | remove Tint | blocked on 07, 02, 03, 05, 06 | |
| 10 | docs | after 09 | lead |

**GPU:** the Oblivion Remastered game was running at 09:35 (8.4 GB of 16 GB VRAM); real-GPU runs wait for it or for the
user's go-ahead.

Kept outside git: `bin/tint_convert_cli.tint-pre-swap` (the Tint converter exactly as of the tag `pre-SPIR-V-Cross`).
