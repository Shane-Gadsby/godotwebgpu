# 82-audio-parity-late-timers

State: open
Owner:
Claimed:
Needs: build (only if a template is rebuilt), browser
Branch: work/82
Results: webgpu_notes/work/results/82-audio-parity-late-timers/

User report (2026-10-29): GitHub CI run https://github.com/Shane-Gadsby/godotwebgpu/actions/runs/40782794737 (🧪 WebGPU
Tests on PR "Webgpu 4.8", head `85d5083a9c`) failed the scene smoketest's `audio_parity` in Chrome:

```
[AUDIO-CHECK] FAIL: position_seek_0.2_plus_0.1 (-0.306, expected 0.300), position_forward_0.45 (-0.957, expected 0.250),
position_forward_pitch_2_0.3 (-1.414, expected 0.200), position_pingpong_0.45 ( ...truncated
AUDIOTEST stream_position_forward_0.45 -0.563
```

The last green run of that workflow was 40272902224 (head `508f4ed3b2`, 2026-10-26).

Lead's reading (verify it): `webgpu_tests/web_audio/fixture_project/main.gd` `_position_after()` corrects a late timer by
subtracting `late * pitch * direction` from the reported position. That only works while the position has neither
clamped at the end of a non-looping stream (`plain` is 0.4 s) nor wrapped in a loop (0.1-0.3 s). On the runner the
timers were ~0.8-1.2 s late, so every such check went negative. Commit `b82368e42e` (2026-10-21) fixed exactly this for
`resumes_playing` and `pitch_change_mid_play` by playing a 2 s sound; the position checks were left with the same flaw.

## Do

1. **Reproduce** the failure locally under CI-like conditions: Chrome as CI runs it (`run_scenes.mjs --browser chrome`,
   swiftshader / fallback adapter, so Forward Mobile), pinned to two cores (`taskset -c 0,1`) to mimic the runner, with
   the current web template. Record the timer lateness the scene sees (add a temporary print, or log `late`).
2. **Make every timing check in `main.gd` valid whatever the lateness**, without loosening what it checks: compute the
   expected position from the loop model (plain clamp, forward, ping-pong, backward loop, pitch, seek offset) at the
   *actual* elapsed time, instead of subtracting the overshoot from a wrapped or clamped position; where a non-looping
   stream has already ended, the measurement cannot say anything — use a longer stream for the plain/seek checks (as
   `b82368e42e` did) rather than skipping. Audit every `_check_near` / `_check_finish` / `late` use in the file, plus the
   streaming (`stream_position_*`) checks, for the same flaw. Keep the native AudioServer side meaningful: the same file
   runs natively (`webgpu_tests/web_audio/run_e2e.mjs`, `export.sh`).
3. **Prove it**: the scene passes repeatedly (≥ 8 runs) under the two-core CI-like setup in Chrome and Firefox (Firefox
   as CI runs it: `xvfb-run`, null-sink sound server), still passes on the real GPU, and still **fails** when the audio is
   actually wrong (e.g. temporarily break `getPlaybackPosition()`'s loop wrap or pitch in `library_godot_audio.js` in a
   scratch export, or fake a wrong expectation) — show both.
4. **Was the runner just slow, or did frames get slower?** Compare the scene's frame time / timer lateness under the
   same local CI-like setup between the current template and one from `508f4ed3b2` *only if* step 1 shows lateness
   near CI's (~1 s); building an old template takes a worktree + ~40 min, so say first what you measured. If frames
   regressed, that is a separate finding for the lead (do not fix it here).

Also run `webgpu_tests/web_audio/run_tests.mjs` (offline) and `run_e2e.mjs` if the fixture changed in a way they read.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (leases, browser launch rule, **never `git stash`**) and CLAUDE.md first. Real-GPU
  runs use the browser launch rule; the CI-like runs deliberately use the fallback adapter the way CI does — say which
  is which in the results.
- Test scope (user): this task's checks only. No engine source change is expected; if one is needed, the dev_mode
  editor and web `template_release` compiles too.
- Commits plain, no Co-Authored-By / Claude-Session trailers. Never push.
- Hand back in `Results:` (SUMMARY.md, for-tasks.md, for-docs.md), set `State: ready-for-integration`.
