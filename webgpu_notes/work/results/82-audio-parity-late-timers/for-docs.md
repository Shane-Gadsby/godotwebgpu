# For the docs (task 82)

Already in the branch: `webgpu_tests/web_audio/README.md` gained a paragraph on how the timing checks stay valid
under late timers (loop model at the elapsed audio-clock time, 8 s sound for non-looping positions, `_late` /
`_error` logs, the native real-driver command). Nothing else is required; suggestions:

## webgpu_tests/README.md (the `web_audio` / scene smoketest entry), one line

> `audio_parity` logs `AUDIOTEST <check>_late` (how late each timer fired, wall seconds) and `<check>_error` (distance
> from the loop model, seconds; the check allows 0.03, Stream playback 0.06): on a CI failure, read those first.

## CLAUDE.md "Web audio" section, one bullet (optional)

> - Timing checks in `fixture_project/main.gd` compare with a model of `AudioServer`'s playback at the time that
>   actually passed on the audio clock (a silent reference sound started with each measured one), never by
>   subtracting a timer's overshoot from a position that may have wrapped or clamped. GitHub's runners fire timers
>   ~1 s late. Natively the checks only run with a real audio driver:
>   `godot --display-driver headless --rendering-driver dummy --audio-driver PulseAudio --path fixture_project`.
