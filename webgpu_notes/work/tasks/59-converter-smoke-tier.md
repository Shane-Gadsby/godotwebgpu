# 59-converter-smoke-tier: TASKS §2.38

State: integrated (TASKS §4.75)
Owner: worker-59
Claimed: 2026-10-28 16:55
Needs: build (wgsl_convert_cli only), gpu (one Chrome validation run)
Branch: work/59
Results: webgpu_notes/work/results/59-converter-smoke-tier/

User request (2026-10-28): begin the remaining tasks. TASKS §2.38: move the converter smoke check out of the work
board into `webgpu_tests/`, as that entry says.

- Move `webgpu_notes/work/smoke.sh` and `smoke_expected_invalid.txt` to a test tier (`webgpu_tests/converter_smoke/`
  is the suggestion; or `spirv_cross_eval/`, say why), with a README section in `webgpu_tests/README.md`.
- **Keep `webgpu_notes/work/smoke.sh` as a two-line forwarder** to the new script: the work board is still in use and
  every worker prompt and task file names that path. Do not rewrite old results or task files.
- The script takes no leases itself (callers do); keep the Chrome launch rule and the adapter assertion as they are
  (§3 trap 79), and make the adapter check's failure message say how to run it on a machine without that GPU if the
  assertion is the only blocker (e.g. an env var to accept another real adapter, never the fallback adapter).
- Point `compare_interface.py`'s and `wgsl_text_scan/run_tests.sh`'s comments and CLAUDE.md's Testing block at it, and
  add it to `local_ci.sh --quick`.
- Run it once from the new place and once through the forwarder; both must print SMOKE OK.
- The CI follow-up in §2.38 (watch the first GitHub run after the swap) needs a push, which the lead does not do:
  write it up for the user in for-tasks.md instead.
