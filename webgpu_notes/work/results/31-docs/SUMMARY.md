# Task 31: docs brought up to date with tasks 24-30 (2026-10-26 evening)

Branch `work/31` off `webgpu-4.8` at `b17c0dbb91`. Docs only; no build, GPU or lease.

## Files touched

- `webgpu_notes/TASKS.md`
  - §1: the 82-arm paragraph replaced by the evening state (87 arms, 87/87 unbaked and baked on the merged tree, which
    arms came from which task, bake gate 150, one line per landed change with its §, traps 70-73). Earlier 82/82 and
    81/82 facts kept inside it as history.
  - §1 Verified tiers: six new rows on top (87/87 parity run; `dof_nan/run.sh`; `bake_isolation/run.sh`;
    `metahuman/run.sh` + `nan_check.gd`; `test_engine_update_label.cpp`; converter smoke after tasks 24-28). The dated
    82/82, 81/82 and 71/71 rows are left as they were, since they were true when written.
  - §2: an "Added 2026-10-26 (evening)" note; both index tables: §2.31 "parity 82/82" -> 87/87, §2.33 points at §2.46,
    §2.39's "what is left" no longer says the gate fails (it passes at the raised 150 ceiling), new rows §2.46 and
    §2.47.
  - New §2.46 (MetaHuman skin follow-ups: cloth shader's normal z, trap 72; lips get pores; the WebGPU after-capture
    that hung twice; the user's `look-skin-pores`) and §2.47 (update check: the two pending user questions
    `update-newer-rule` / `update-repo`, the commit-date blind spot, the coupling to `runner.yml`'s `main-<sha>` tag and
    asset names). §2.33's open bullet points at §2.46; §2.39's "Parity itself is 82/82" -> 87/87.
- `CLAUDE.md`: Testing gets `webgpu_tests/dof_nan/run.sh` and the `--test --test-case="*EngineUpdateLabel*"` unit test
  (the lead had already done the 87-arm line, the bake-scope paragraph, the MetaHuman rules and the update-check
  pointer).
- `README.md`: "passed all 82" -> 87 (unbaked and baked); a sentence after the feature table on white balance and NaN
  containment being covered; two engine-additions bullets (bake isolation, the update check).
- `drivers/webgpu/README.md`: the export-time bake's "runs with the target's capabilities" bullet now says the
  override is scoped (`ShaderBakeFeatureOverrideScope`, per thread, the three baker callbacks), what
  `shader_bake_feature_override_is_active()` still means, and the groups/refresh-callback rule.
- `webgpu_site/TECHNICAL_REFERENCE.md`: §4.5 gets the same scoping paragraph; §7.4's shader-change table gets rows for
  `finite_inc.glsl` and the white-balance gains filling the 128-byte tonemap push constant.
- `webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md`: ship-readiness row "all 82 matrix arms" -> 87, unbaked and baked.
- `webgpu_tests/README.md`: tier table rows for DOF NaN containment, shader-bake isolation and the MetaHuman importer
  (sections 13 and 14 already existed).
- `webgpu_tests/forward_plus/README.md`: a paragraph on the four NaN `maxDelta` arms and why they raise the bake ceiling.
- `webgpu_notes/work/STATUS.md`: a dated late-evening entry at the top; no task row changed.
- `webgpu_notes/work/results/31-docs/for-wiki.md`: the wiki is current; two optional additions (Shader Baking page:
  the bake answers as the target only inside the baker; MetaHuman page: the clothing normal-z limitation until §2.46).

## Deliberately left

- §4 entries and dated rows (§4.39's 82, §4.41's "84 arms with §4.42's", §4.36's 71/71, ARCHITECTURE/CORRECTNESS's
  "71/71 after the swap"): true when written.
- §4.41's "Open: glow, SSR and auto-exposure..." bullet: history; §4.44 closes it right below.
- §2.39's first paragraph (`translated <= 80` fails): the original text, followed by its dated 130 and 150 notes.
- `.github/copilot-instructions.md`: dwalter-era file (Emscripten 5.0.0, `/Users/dwalter/...` paths), stale well beyond
  today's work; it needs a rewrite or deletion, which is the user's call.
- `README.md`'s dwalter mentions: history and credits, correct.
- `docs/`: only `docs/braced_gdscript/`, which today's tasks did not touch.
- The wiki: not mine; see `for-wiki.md`.
