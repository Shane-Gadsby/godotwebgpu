# Work board: the Tint → SPIRV-Cross swap, run by a lead and parallel workers

The swap of webgpu_notes/TASKS.md §2.35 is done by one lead session and several worker agents at once. This folder
is how they share the work without stepping on each other. It is modeled on the Oblivion co-op project's board
(`/mnt/109313D2109313D2/projects/oblivion-coop/docs/work/`), including the lessons that board cost. **Read this whole
file before doing anything.** TASKS.md stays the project's record: the lead folds finished work into §2.35 / §4; this
board is the coordination layer for the swap and is retired when the swap closes.

**The goal, and what counts as done:** Tint is gone from the build (runtime, the converter tool — renamed
`wgsl_convert_cli` —, the precompiler, the export-time bake, tests, CI, docs; `thirdparty/tint` deleted) and SPIRV-Cross's
WGSL backend (`thirdparty/spirv-cross-wgsl`, patched locally) does every SPIR-V → WGSL conversion. The bar for deleting
Tint (user, 2026-10-25, on the hand-offs page):
- Forward+ at full parity against native Vulkan (`webgpu_tests/forward_plus/run_forward_plus.mjs --parity`: every feature
  that passed before the swap passes after it), and no test tier worse than at the tag `pre-SPIR-V-Cross`;
- the user's cameraSim project (`~/Downloads/cameraSim_2026-09-09_10-26-06/testing`) judged by eye, WebGPU beside Vulkan;
- **Firefox (desktop and Android) and Chrome on Android work, and are ideally as fast as with Tint or faster** — a
  performance bar, measured against Tint-baked exports, not only a correctness one (desktop on the 4080 SUPER, Android on
  the Razr 60 over adb, TASKS.md §2.9 and memory "Android phone profiling").

**SPIRV-Cross patches are never upstreamed** (user, 2026-10-25): no PRs, issues or comments to Ludicon or Khronos.
Patch the vendored copy, record each patch in `thirdparty/spirv-cross-wgsl/patches/` and `thirdparty/README.md`.

## Layout

- `webgpu_notes/work/tasks/<id>.md`: one file per task. The header has the task's state; the body says what it is,
  what it needs, what "done" means and **how it is verified**. A task file is edited only by its owner (and by the
  lead when integrating or re-planning).
- `webgpu_notes/work/results/<id>/`: anything a task produces that isn't code (notes, findings, logs, test output).
  Only that task's owner writes there. Keep it small: logs over ~1 MB go in a scratch directory, not git.
- Code: each task works on its own branch `work/<number>` (the `Branch:` line) in its own git worktree:
  `git -C /mnt/109313D2109313D2/godot-editors/godotwebgpu worktree add ../godotwebgpu-<id> -b work/<number> webgpu-4.8`.
  Never commit task work to `webgpu-4.8`; the lead merges finished branches after verifying them.
- `webgpu_notes/TASKS.md`, `CLAUDE.md`, `README.md`, `drivers/webgpu/README.md`, `webgpu_site/*`, `thirdparty/README.md`
  and this board's `README.md` / `STATUS.md`: edited **only by the lead** (shared, conflict easily). Put what should go
  into them in your results folder as `for-tasks.md` / `for-docs.md`; the lead copies it in.
  Exception: a task that adds a SPIRV-Cross patch writes its own `thirdparty/README.md` line in `for-docs.md` and its
  patch file under `thirdparty/spirv-cross-wgsl/patches/` on its branch.

## Task states (the `State:` line)

`open` → `claimed` → `ready-for-integration` → `integrated` (lead, after verification) or `blocked`.

**Never run git in the lead's checkout** (`/mnt/109313D2109313D2/godot-editors/godotwebgpu`). The lead and possibly
another session work there; a commit or checkout from you lands under their feet. Everything you do, including the
task-file and results commits, happens in your own worktree.

**Nobody pushes, ever** — not the lead, not a worker, not a branch (user rule for this repository). Branches are local;
every worktree sees them through the shared repository.

**Never force git past a refusal.** If git says a branch is already used by another worktree, that is git stopping
real damage: do not reach for `-f`, `--force` or `--ignore-other-worktrees`. A commit that deletes 20 or more tracked
files, or whose tree equals an older commit's, is a revert in disguise: stop and ask the lead.

To **claim**: make your worktree, then in it set `State: claimed`, `Owner: <your agent name>`, `Claimed: <date time>`
in the task file and commit that with your first work. If the lead already put your name on `Owner:`, go straight to
the work. Ask the lead before starting a task somebody else's `Owner:` names.

When **done**: write `webgpu_notes/work/results/<id>/SUMMARY.md` (what changed, how it was verified with the actual
numbers, what the lead must still run, risks), set `State: ready-for-integration`, commit both on your branch. That is
the whole hand-back: report in ~10 lines and stop.

## Questions for the user: the WebGPU Hand-offs page

Anything only the user can settle goes on **https://claude.ai/artifact/ULPkGBh3V4gRFSoujWs4pP** (source:
`webgpu_notes/work/webgpu-handoffs.html`; modeled on the co-op project's Rig Hand-offs board), never only into chat:
decisions (a pick with a marked default, so "all defaults" is a complete answer), typed answers, and looks (frames to
judge by eye, uploaded as artifact assets, WebGPU beside Vulkan). Its data is the artifact database (`ArtifactData`):
collection `questions` (doc id = question id; `kind` choice | text | look, `order`, `visible`, `title`, `ref`,
`question`, `context`, `choices[{label, detail}]`, `default`, `media[{url:"/_blob/<id>", kind, caption}]`,
`placeholder`, and the user's `answer`, `note`, `updated`, `told`) and `meta/board` (`message`, `updatedAt`,
`busy_gpu`, `busy_text`). A look with no media shows as queued. Workers put a question in `for-tasks.md` under
"Questions for the user"; only the lead writes the page. Set `busy_gpu` while a real-GPU run is going.
The page is light-only (user, 2026-10-25). Its controls are never disabled from `user.can("data.write")` (advice only;
a refused write decides and the page says why on screen). Two traps cost an hour (2026-10-25): the runtime's db records are
frozen, so `d.data().id = ...` is silently ignored (copy with `Object.assign({}, d.data(), {id: d.id})`); and the viewer
re-delivers unchanged snapshots, so a page that rebuilds its buttons on every delivery swallows clicks (press and release
land on different elements). Rebuild only changed markup. Neither shows in a local stub test unless the stub freezes
records and re-delivers. The page's script must stay ASCII (write `\u2028`-style escapes as escapes, check with `node --check` before
publishing): a raw U+2028 in a regex once blanked the co-op board.

## Shared resources (leases)

`webgpu_notes/work/lease.sh take|drop|show <name> <owner> [hours]`. Leases live in `~/.cache/godotwebgpu-leases/`.

- `gpu`: the real GPU for **rendering or timing** — `run_forward_plus.mjs`, the scene smoketest, `perf_guard`,
  `gpu_time.mjs`, screenshot comparison. One holder at a time: two real-GPU runs at once contend for VRAM and the
  numbers look like regressions (TASKS.md §3 "Measurement traps"). Before taking it, check `nvidia-smi`: if the
  Oblivion Remastered game or LM Studio holds most of the 16 GB, say so to the lead instead of running. Compiling WGSL
  in Chrome (`validate_wgsl.mjs`, `smoke.sh`) is not rendering and needs no lease.
- `phone`: the Razr 60 over adb (task 12). Separate from `gpu`: the phone's GPU does not contend with the desktop's, so a
  phone run may go beside a desktop one. Desktop reference captures for a phone task still take `gpu`.
- `build`: any SCons run (editor or web template) in any worktree. One at a time: each saturates all 24 cores, and
  `-j24` twice over runs both slower than one after the other. `tint_convert_cli`'s `build.sh` (~30 s) needs no lease.
  Still obey CLAUDE.md inside a tree: never start SCons while another runs in the same tree (`pgrep -ax scons`).

## Rules

- **Verification is the bottleneck, and merging is not progress** (co-op lesson 1). A task is `integrated` only after
  the lead ran its verification on the merged tree. The merge rate is capped by the verification rate.
- **Smoke after every merge, not per batch**: `webgpu_notes/work/smoke.sh` on the merged tree before the next merge.
- **Run `webgpu_notes/work/smoke.sh <scratch_dir>` before you set `ready-for-integration`** and paste its last lines into
  SUMMARY.md. It forwards to `webgpu_tests/converter_smoke/run.sh`: builds `wgsl_convert_cli` from your tree, converts
  every engine variant `wgsl_precompile.py` builds, compares bindings, override ids and the driver's text-scan facts with
  the stored references, and compiles every result in Chrome.
  If it fails, the task is not ready. A known failure you cannot fix is listed in SUMMARY.md, never silently.
- **Tools that report success without checking are the worst defect class** (co-op lesson 6): a text edit asserts its
  match count, a wait that times out fails, a skip is reported as a skip, never counted as a pass.
- **Parity is judged against native Vulkan, not against Tint's WGSL.** Tint is the reference for interface
  compatibility only (bindings, override ids); where Tint and Vulkan disagree, Vulkan wins (CLAUDE.md "Parity, not just
  it changed"; TASKS.md §3 trap 28).
- **Browser launches (user, 2026-10-28: a worker's flagless runs caused WebGPU failures). Every launch, every script,
  no exceptions:** Chrome = `{ executablePath: '/usr/bin/google-chrome-stable', headless: false, args:
  ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'], ignoreDefaultArgs:
  ['--enable-unsafe-swiftshader'] }` (never bundled Chromium, headless, `--use-angle=vulkan`, `--use-vulkan=native` or
  `--no-sandbox`); Firefox = `{ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS }` from
  `webgpu_tests/scene_smoketest/firefox_prefs.mjs`. Assert the adapter at the start of each run (Chrome: nvidia/lovelace,
  not fallback, `maxSampledTexturesPerShaderStage` 48; Firefox: `requestAdapter()` non-null) and fail otherwise. Never set
  `CI` for real-GPU runs (`run_forward_plus.mjs` goes headless with it). Software adapters run Forward Mobile (CLAUDE.md).
- **Worker retirement at 350k tokens**: a worker whose context reaches 350k finishes the task in hand (SUMMARY,
  `ready-for-integration`, commit) and stops. The lead never resumes a worker past 350k; follow-ups go to a fresh worker,
  which catches up from `results/`.
- Commit messages: plain, no `Co-Authored-By` or session trailers (user rule for this repository).
- Match the code's style (CLAUDE.md "Godot Coding Conventions"; comments say why). `pre-commit run --files <changed>`
  before committing C++.
- Long runs go in a script that writes results to disk; keep each wait under ~5 minutes with a status line between.

## Startup prompt for a worker

> You are a worker on the Godot WebGPU Tint → SPIRV-Cross swap (repo /mnt/109313D2109313D2/godot-editors/godotwebgpu,
> base branch webgpu-4.8). Read webgpu_notes/work/README.md completely, then CLAUDE.md, webgpu_notes/TASKS.md §2.35
> and your task file webgpu_notes/work/tasks/<id>.md. Work on its branch in your own worktree (never git in the lead's
> checkout, never push, never force). Non-code outcomes go in webgpu_notes/work/results/<id>/. Run
> webgpu_notes/work/smoke.sh before handing back; finish with SUMMARY.md and `State: ready-for-integration` committed on
> your branch. Don't edit shared docs; take the `gpu` / `build` lease before rendering runs or SCons builds. Report in
> ~10 lines and stop.

## Startup prompt for the lead

> You lead the swap. Read this README, STATUS.md and TASKS.md §2.35. Merge `ready-for-integration` branches one at a
> time into webgpu-4.8, run smoke.sh after every merge, run each task's verification (the GPU ones under the `gpu`
> lease), mark tasks `integrated`, fold their for-tasks/for-docs into the shared docs, keep STATUS.md and the task list
> current, and spin off workers for open tasks.
