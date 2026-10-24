# Status (lead-maintained)

## 2026-10-25 10:00 — task 04 integrated

- **04 integrated** (merge f62806e2e3): `spec_constants_overridable()` unchanged on evidence; engine: 183 variants
  without spec constants, 93 overridable (216 overrides), 0 frozen, same under both backends. 44 probe modules mapped
  what SPIRV-Cross lowers (SUMMARY in results/04). New: `tint_convert_cli --spec-info` / `--raw`,
  `spirv_cross_eval/run_spec_ops.sh`. Docs text waits in results/04/for-docs.md for task 10.
- **Follow-up found, not swap-caused:** `freeze_spec_constant_ops()` folds CompositeExtract / CompositeInsert /
  VectorShuffle of a spec composite to 0 (same with Tint). No engine shader takes that path; a user shader could.
  Goes into TASKS.md §2 at task 10 unless a worker picks it up first.

## 2026-10-25 09:58 — task 01 integrated

- **01 integrated** (merge 6bfc543fd2): SPIRV-Cross patch 0001, barrier/load/barrier on a workgroup variable becomes
  `workgroupUniformLoad`; smoke on the merged tree 276/276 valid in Chrome, interface identical, expected-invalid list
  empty. Only the FSR2 `no_image_atomics` module's WGSL changed. FSR2 still has to be seen rendering (task 07).
- Worktree traps worker-01 hit, both fixed on webgpu-4.8: `validate_wgsl.mjs` finds Playwright in the main checkout
  when a worktree has no node_modules; `build.sh` throws away SPIRV-Cross objects after any SPIRV-Cross header edit
  (stale objects gave `free(): invalid pointer`). Workers on older bases: merge webgpu-4.8 into your branch for these.

## 2026-10-25 09:50 — workers 01-06 out; first exports

- Workers spawned 09:41 for tasks 01-06 (one worktree each, `../godotwebgpu-0N`, branch `work/N`).
- Their six first `build.sh` runs at once peaked at ~250 compilers, 26 GB, load 147 (available memory fell to ~10 GB
  with the game + Ghidra up); it recovered within two minutes. `build.sh` now takes `JOBS=<n>`; worktrees building at
  once should use `JOBS=6`.
- Task 07 exports made (fp_check, baked, existing editor/templates from 2026-10-23, bake cache off): Tint
  `index.shaders.pck` 29.6 MB raw / 4.43 MB gzip -9; SPIRV-Cross 34.8 MB raw / **3.70 MB gzip** (−17% compressed).
  Parity runs wait for the GPU (the game holds ~8-10 GB VRAM).

## 2026-10-25 09:40 — board opened

Foundation integrated (task 00, see its file): `thirdparty/spirv-cross-wgsl` vendored, `spirv_cross_wrapper`,
`tint_convert_cli` builds on SPIRV-Cross by default (`WGSL_BACKEND=tint` for the reference), `smoke.sh` OK on the lead
tree (276/276 converted, interface identical, 275/276 valid — FSR2, task 01).

| task | what | state | owner |
|---|---|---|---|
| 00 | foundation | integrated | lead |
| 01 | FSR2 barrier uniformity (SPIRV-Cross patch) | integrated (render check in 07) | worker-01 (retired) |
| 02 | driver WGSL text scans | open | |
| 03 | runtime converter in wasm, exceptions, SCsub | open | |
| 04 | spec constants | integrated | worker-04 (retired) |
| 05 | real projects' shaders | open | |
| 06 | offline test tiers | open | |
| 07 | Forward+ parity vs Vulkan | open | lead (gpu) |
| 08 | GPU timing | open | |
| 09 | remove Tint | blocked on 07, 02, 03, 05, 06 | |
| 10 | docs | after 09 | lead |

**GPU:** the Oblivion Remastered game was running at 09:35 (8.4 GB of 16 GB VRAM); real-GPU runs wait for it or for the
user's go-ahead.

Kept outside git: `bin/tint_convert_cli.tint-pre-swap` (the Tint converter exactly as of the tag `pre-SPIR-V-Cross`).
