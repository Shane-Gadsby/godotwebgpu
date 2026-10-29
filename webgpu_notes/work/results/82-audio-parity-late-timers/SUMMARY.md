# Task 82: audio_parity's timing checks hold however late a timer fires (SUMMARY)

Written by the lead from worker-82's report (the worker's harness refused to create this file). Branch `work/82`:
`af7f73139a` (claim), `a292a4748a` (the fix). Only `webgpu_tests/web_audio/fixture_project/main.gd` and a paragraph in
`webgpu_tests/web_audio/README.md` changed; no engine source.

## What was wrong

1. `_position_after()` subtracted the timer overshoot from a position that had already clamped at the end of the
   0.4 s sound or wrapped in a loop. Reproduced with Chromium launched exactly as `run_scenes.mjs` does under `CI=1`
   (fallback swiftshader adapter, `--disable-audio-output`), pinned to two cores: the same nine checks failed as on
   GitHub (run 40782794737), 2/2 runs, timers 0.15-0.69 s late.
2. The audio clock is not the wall clock under load. A first fix (expected position from wall-clock elapsed time) still
   failed `position_forward_pitch_2_0.3` in 2 of 8 CI-like runs: Chrome's fake audio output, starved of CPU, falls
   about 50 ms behind the wall clock in 0.8 s.
3. A late read picks which part of a loop gets checked: a deliberately broken ping-pong return passed a CI-like run
   because the late read landed in the forward half.
4. Sounds start late: under load an Ogg/MP3 sound starts 10-40 ms after one started just before it (its `play()` sets
   up a decoder first), which caused one spurious 41 ms `mp3_loop_position` failure.

## The fix

- **Loop model:** each position is compared with a model of how `AudioServer` plays the sound (WAV forward /
  ping-pong / backward loops, Ogg/MP3 loop offset, pitch, start offset). A non-looping sound that has already ended
  fails with a message saying so; it is never skipped.
- **Audio clock:** elapsed time comes from a silent 8 s, 44.1 kHz reference sound started with each measured sound;
  its rate differs from the 22.05 kHz test sounds, so a resampling bug in one cannot cancel out. Real-time rate against
  the wall clock stays covered by the unchanged finish checks.
- **Start lag:** each sound's start lag is measured right after `play()` and taken off, capped by the clock's own
  reading, so it cannot hide a wrong start position.
- **Copies across the loop:** every looped check starts 2-4 copies spread across the loop, so whatever the lateness,
  one copy reads in each phase (`<check>_from_<start>`).
- **Longer sounds:** non-looping position checks play an 8 s sound instead of 0.4 s / 2 s.
- **Two more checks with the same flaw:** `player_2d_playing` and `polyphonic_playing` are checked right after `play()`,
  plus new per-frame finish times (`player_2d_finish`, `polyphonic_pitch_2_finish`).
- **New logs:** every check logs `AUDIOTEST <check>_late` and `<check>_error`, so a CI log shows the lateness.

## Results

| Run | Result | Timers late (max) | Max error (allowed 0.030 s) |
|---|---|---|---|
| CI-like Chrome, 2 cores | 8/8 PASS, plus 2/2 on the final tree | 1.08 s | 0.003 s |
| CI-like Chrome, 1 core (harsher than CI) | 4/4 PASS | 1.96 s | 0.003 s |
| CI-like Firefox (xvfb-run, null-sink PulseAudio, lavapipe, 2 cores) | 8/8 PASS, plus 1/1 final | 0.045 s | 0.003 s |
| Real-GPU Chrome | 8/8 PASS, plus 1/1 final | 0.007 s | 0.003 s |
| Real-GPU Firefox | 8/8 PASS, plus 1/1 final | 0.010 s | 0.003 s |
| Real-GPU Chrome, threaded export | 2/2 PASS | 0.008 s | 0.003 s |
| `run_scenes.mjs` itself, `CI=1`, 2 cores | PASS | | |

Still fails on wrong audio (breaks patched into copies of the exported `index.js`, each failed every run: 2 CI-like
plus 1 real-GPU Chrome):

| Break | Caught by |
|---|---|
| Loop end clamps instead of wrapping | the forward, ping-pong, backward and Ogg/MP3 loop checks |
| Pitch ignored in the position | `pitch_change_mid_play` every run; the pitch-2 loop check can miss it at some lateness |
| Ping-pong return not mirrored | 2 of the 4 `position_pingpong_0.45*` copies every run |
| Backward loop position +50 ms | both backward checks |

Also: native headless passes; the `run_e2e.mjs` comparison (reproduced with a compliant launcher, since `run_e2e.mjs`
launches headless Chrome with `--use-angle=vulkan`) has every native key on web with matching booleans, 92 keys;
converter smoke `SMOKE OK`; pre-commit passes on the changed files.

## Findings

- **Native MP3 ends ~50 ms early:** `tone.mp3` finishes at 0.497 s against `get_length()` 0.548 s natively with a real
  audio driver, failing `mp3_loop_position*` with the old `main.gd` too; the web matches the model. Headless runs never
  caught it because the Dummy driver skips timing checks. §2 draft in `for-tasks.md`.
- **`generator_no_skips` fails in a threaded export starved to two cores** (old `main.gd` too); CI exports only the
  nothreads preset, where it passes. Left unchanged.
- **Step 4 not done:** the old suite peaked at 0.69 s lateness locally with two cores, below CI's ~1 s, so no old
  template was built to compare frame times.
- codespell's auto-fix rewrote a GDScript variable `froms` into `forms`; renamed to `starts` (trap candidate).

Scratch: `/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w82/`
(`runs*/`, runner `tools/runaudio.mjs`).
