# Status (lead-maintained)

## 2026-10-25 11:10 — 03 and 02 merged; merged tree builds and smokes

- **03 merged** (e05c777b8d): in-engine converter on SPIRV-Cross (`webgpu_wgsl_backend=spirv-cross|tint`,
  `-fwasm-exceptions` on its TUs + link), all 8 web variants linked on its branch; release nothreads wasm 1.6% smaller
  raw / 2.0% gzip than Tint (SPIRV-Tools stays). Booted on swiftshader only (translated=502).
- **02 merged** (2c2ebe96a1, include-order conflict with 03 resolved): `wgsl_text_scan.{h,cpp}` replaces ~1,500 lines of
  `strstr` scanning; found and fixed SSR downsample's `source_depth` (SPIRV-Cross's `let _N = textureLoad(...)` shape)
  that would have got the blank depth fallback.
- Merged tree (2c2ebe96a1): `build-linux.sh` editor + web release nothreads, 4 min, no warnings in the touched files;
  smoke OK (276/276 valid, interface identical); `wgsl_text_scan/run_tests.sh` 276/276 both modes.
- `bin/` now holds the editor + template from 2c2ebe96a1 (the parity run in flight uses copies of the 2026-10-23 pair).
- ccache: build-linux.sh already exports `CCACHE_MAXSIZE=50G`; the config limit only bit SCons runs outside it (now 20 GB,
  before 5 GB — why the cache sat full). Question on the page: match 50 GB.

## 2026-10-25 11:05 — task 05 merged

- **05 merged** (5ec31c3aa4; smoke after the parity run frees the GPU). Real projects through both converters: fp_check
  882 stages, sewer-shift 1224, cameraSim 1318, smoketest projects 1936 (2566 unique): SPIRV-Cross converts every stage
  Tint does, interfaces identical, 2562/2566 valid in Chrome, output 1.09-1.14x Tint's size. Harness:
  `spirv_cross_eval/project_export.sh` + `project_eval.py`. Its patch 0005 duplicated 0006 (same 16-bit norm formats,
  written in parallel) and was dropped at merge.
- **New task 15**: the 4 screen-space contact shadow variants fail barrier uniformity (builtins copied to
  `var<private>`); a Forward+ feature, so expect parity to flag contact shadows.
- Question for the user (on the page): a MetaHuman export to add to the corpus, or skip.

## 2026-10-25 10:50 — the user's answers (hand-offs page, all twelve)

- GPU: game closed, go ahead → task 07 parity running (gpu lease: lead), Tint baseline then SPIRV-Cross.
- Acceptance bar widened (README goal): + Firefox, + Razr 60, + cameraSim by eye; Firefox and Android Chrome must be
  as fast as Tint or faster → tasks 08 (desktop Chrome), 11 (Firefox), 12 (Android) measure against Tint-baked exports.
- Rename `tint_convert_cli` → `wgsl_convert_cli`; delete `thirdparty/tint` once the bar holds → task 09.
- Harden the passes against malformed SPIR-V now → task 13. Fix the spec-composite fold inside the swap → task 14.
- Workers: Opus 5, effort medium. The Agent tool cannot select Opus 5; the user sets the default sub-agent model in
  /config. New workers (13, 14, then 08, 11, 12) wait for that.
- ccache: 20 GB (`ccache -M 20G`; the user asked whether 20 is enough instead of 40 — start at 20, watch evictions) and
  `base_dir=/mnt/109313D2109313D2/godot-editors` so worktrees share it. Before: 5.6 GB used, 72% hits.
- Worktrees removed after merge (branches kept): -01, -04, -06.
- sewer-shift: `/mnt/109313D2109313D2/godot-game-dev/sewer-shift`; cameraSim: `~/Downloads/cameraSim_2026-09-09_10-26-06/testing`
  → sent to worker-05.

## 2026-10-25 10:15 — task 06 integrated

- **06 integrated** (merge bead486f41). On the merged tree: smoke OK (276/276 valid, interface identical);
  preprocessing_tests 291 passed / 1 skipped with both converters (32 assertions had matched Tint's spelling); driver
  unit tests 379/0; spec_constant_overrides 8/8; shader_corpus 14/14. Every offline tier takes `TINT_CONVERT_CLI=<path>`.
  SPIRV-Cross patch 0006 (16-bit norm storage formats; CanvasSdf had regressed). Fuzzers fixed (they were broken before
  the swap) and ported.
- Findings: fuzz asserts in SPIRV-Tools on malformed SPIR-V in the shared passes (both backends, low priority,
  reproducers in results/06/fuzz_repro) → TASKS.md §2 at task 10. SPIRV-Cross's `const` arrays vs Tint's `var<private>`
  (SSIL hoist, §2.30) → task 08 checks it first.
- Not run anywhere yet: every render tier (GPU busy with the game).

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
| 02 | driver WGSL text scans | merged, builds, smoke OK; parity pending | worker-02 (retired) |
| 03 | runtime converter in wasm, exceptions, SCsub | merged, builds, smoke OK; GPU boot pending | worker-03 (retired) |
| 04 | spec constants | integrated | worker-04 (retired) |
| 05 | real projects' shaders | merged, smoke pending | worker-05 (retired) |
| 06 | offline test tiers | integrated | worker-06 (retired) |
| 07 | Forward+ parity vs Vulkan | open | lead (gpu) |
| 08 | GPU timing | open | |
| 09 | remove Tint | blocked on 07, 02, 03, 05, 06 | |
| 10 | docs | after 09 | lead |
| 11 | Firefox desktop: works, as fast as Tint | open | |
| 12 | Android Chrome + Firefox (Razr 60): works, as fast as Tint | open | |
| 13 | harden passes against malformed SPIR-V | open | |
| 14 | spec-composite fold fix | open | |
| 15 | contact shadows barrier uniformity (SPIRV-Cross patch) | open | |

**GPU:** the Oblivion Remastered game was running at 09:35 (8.4 GB of 16 GB VRAM); real-GPU runs wait for it or for the
user's go-ahead.

Kept outside git: `bin/tint_convert_cli.tint-pre-swap` (the Tint converter exactly as of the tag `pre-SPIR-V-Cross`).
