# For TASKS.md (task 82)

## §4 entry

### 4.9x audio_parity's timing checks hold however late a timer fires (October 2026) *(work task 82)*

GitHub CI failed the scene smoketest's `audio_parity` in Chrome (run 40782794737): its timers fired ~1 s late on a
software-rendering runner, and `_position_after()` in `webgpu_tests/web_audio/fixture_project/main.gd` "corrected" a
reading by subtracting the overshoot from a position that had already clamped (0.4 s non-looping sound) or wrapped
(0.1-0.3 s loops), so nine position checks went negative. Reproduced locally with bundled Chromium on the fallback
adapter exactly as CI launches it, pinned to two cores (`taskset -c 0,1`): the same nine checks failed with timers
0.15-0.69 s late. Now every position is compared with a model of `AudioServer`'s playback (`_model_position()`: WAV
forward / ping-pong / backward loops, the Ogg/MP3 loop back to `loop_offset`, pitch, start offset; -1 once a
non-looping stream has ended, which fails explicitly) at the time that actually passed, measured on the **audio clock**
(a silent 8 s, 44.1 kHz reference sound started with each measured one; Chrome's fake audio output under CPU starvation
fell ~50 ms behind the wall clock in 0.8 s), minus each sound's measured start lag (an Ogg/MP3 `play()` sets up a
decoder before its sound starts; bounded by the clock's own reading, so it cannot absorb a wrong start). Non-looping
positions play an 8 s sound; every looped check starts copies spread across the loop period (half a loop for forward
and backward, a quarter cycle for ping-pong), because a late read picks the phase it verifies and one phase can hide a
broken mapping. `player_2d_playing` / `polyphonic_playing` became finish times (`player_2d_finish`,
`polyphonic_pitch_2_finish`), which a long frame cannot overrun. Each check logs `<check>_late` and `<check>_error`.
Results (scratch exports of this tree with the current templates): CI-like Chrome 8/8 (timers up to 1.08 s late, model
error ≤ 3 ms against 30 ms allowed), one core 4/4 (~1.9 s late), CI-like Firefox (xvfb, null-sink PulseAudio, lavapipe
fallback adapter, two cores) 8/8, real GPU Chrome 8/8 and Firefox 8/8; four deliberate breaks of
`library_godot_audio.js`'s position mapping (wrap clamped, pitch ignored, ping-pong return unmapped, backward offset
+50 ms) each failed every run in both CI-like and real-GPU Chrome. The first version of the fix (wall clock, one copy
per loop) let the ping-pong break pass a CI-like run — which is why the copies exist. Native with a real audio driver
(`--display-driver headless --rendering-driver dummy --audio-driver PulseAudio`) agrees with the model except
`mp3_loop_position`, which reads 40-60 ms ahead natively, before this change too (see below).

## Trap candidates

- **Never correct a late timer by subtracting the overshoot from a position** (task 82): once the position has wrapped
  or clamped, the subtraction produces nonsense (negative positions on CI). Compute the expectation from a model at the
  time that actually passed, and read that time off the audio clock — a starved browser's audio clock is not the wall
  clock. And on a loop a late read chooses the phase it verifies: start copies across the period.
- **A headless native run never checks audio timing** (task 82): `--headless` selects the Dummy audio driver, under
  which `main.gd` only logs numbers. `--display-driver headless --rendering-driver dummy --audio-driver PulseAudio` runs
  them against real native playback; it found the native MP3 offset below, invisible until then.

## For §2 (new, small, native-only, not fixed here)

- **Native MP3 playback ends and loops ~50 ms before `AudioStreamMP3.get_length()`**: natively (PulseAudio driver)
  `tone.mp3` finishes at 0.497 s against a reported length of 0.548 s, and its loop reads 40-60 ms ahead of a loop
  of `get_length()`; the web's Sample rendering loops at `get_length()` and matches the model. One of the two is wrong
  for parity (likely the MP3 decoder's padding/delay handling vs the length computation). `mp3_finish` passes natively
  only because its tolerance (50 ms + a frame) absorbs it. Needs a look at `modules/minimp3` (or wherever the MP3
  stream lives in this tree) against an MP3 with known padding.

## Step 4 (frame-time regression since 508f4ed3b2): not measured, by the task's own condition

The task asked for an old-template comparison only if local lateness came near CI's ~1 s. With two cores it peaked at
0.69 s (before) / 0.66-1.08 s (after: the new suite adds more players); CI's was ~0.8-1.2 s. So no old template was
built. The `_late` logs now in every check make the next CI run show its lateness directly; comparing that with the
`508f4ed3b2` era needs an old template or an old CI log with those lines (none exists).

## Questions for the user

None.
