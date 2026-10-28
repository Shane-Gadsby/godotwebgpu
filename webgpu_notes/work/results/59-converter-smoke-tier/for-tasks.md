# For TASKS.md (task 59)

## §4 archive entry (replaces §2.38)

### 4.xx Converter smoke check is a `webgpu_tests` tier (October 2026) *(was §2.38; work task 59)*

`webgpu_notes/work/smoke.sh` and `smoke_expected_invalid.txt` moved to `webgpu_tests/converter_smoke/run.sh` and
`expected_invalid.txt`; the old path is a two-line forwarder, since the work board's prompts and task files name it. It
builds `wgsl_convert_cli`, converts every engine variant `wgsl_precompile.py` builds, compares interfaces with
`spirv_cross_eval/interface_reference.json` and the driver's text-scan facts with `wgsl_text_scan/scan_reference.txt`, and
compiles every module in Chrome; `local_ci.sh` runs it in stage 1, `--quick` included. `validate_wgsl.mjs` now asserts
the adapter (Chrome nvidia/lovelace, not fallback, 48 sampled textures; exit 3) instead of only logging it;
`WGSL_VALIDATE_ADAPTER=<vendor>/<arch>` or `=any` accepts another real adapter, never the fallback one (§3 trap 79).
Verified 2026-10-28 from both paths: 311/311 compiled, interfaces 311/311, scan facts 622/622, Chrome 311 valid,
SMOKE OK, ~10 s a run. The GitHub half of the old §2.38 (watch the first CI run after the swap) is a §2 item of its own,
below, because it needs a push.

## §2 entry to add (the CI half of the old §2.38)

### 2.xx First GitHub run after the converter swap `OPEN` — needs the user's push *(from §2.38, §4.36)*

Work task 09 renamed CI's matrix key (`wgsl-cli`), the converter artifact (`wgsl-convert-cli`) and the cache keys of
every workflow that builds it, and no workflow has run on GitHub since: the lead and workers never push. **For the user:**
on the first push of `webgpu-4.8`/`main` after the swap, watch 🧪 WebGPU Tests and 🔗 GHA for (1) a job that downloads
the old `tint-convert-cli` artifact name or expects `bin/tint_convert_cli` (an artifact-not-found or missing-file error
in the shader-corpus / precompile steps), (2) a cold cache on every job the first time (expected: the keys changed;
only a second cold run is a problem), and (3) the `dev_mode=yes` web build of the SPIRV-Cross wrapper (it is the one
part of the engine built with `-fwasm-exceptions`). If 1 shows up, grep `.github/` for `tint`. Effort: one CI run's
watching, ~15 min.

## Other docs

- CLAUDE.md: already updated on the branch (Testing block, stored-references paragraph).
- `webgpu_notes/work/README.md` (lead-only): lines 100–103 and 132 may name `webgpu_tests/converter_smoke/run.sh`
  instead; the old path keeps working, so this is optional while the board lives. Line 101's description ("builds
  `tint_convert_cli` with both backends") is stale since the swap: it builds `wgsl_convert_cli` and compares with the
  stored references.
- TASKS.md §1's table row "converter smoke (`webgpu_notes/work/smoke.sh`)" can name the new path.

## Questions for the user

None.
