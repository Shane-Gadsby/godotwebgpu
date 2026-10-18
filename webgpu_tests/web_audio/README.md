# Web audio parity

Checks that the web export's audio does what native Godot does. On web, `AudioStreamPlayer`s default to
**Sample** playback (`audio/general/default_playback_type.web`), where the engine hands each sound to the browser
and Web Audio plays it — so `AudioServer`'s own mixer, which every native platform uses, is bypassed and its
behavior has to be reproduced by `GodotSampleDriver` in `platform/web/js/libs/library_godot_audio.js`. These
tests compare the two.

## `run_tests.mjs` — the sample driver, sample for sample (no build)

```bash
npm install
node run_tests.mjs [--filter <substring>]
```

Loads `library_godot_audio.js` straight from the tree into a Chrome page and renders the real driver in an
`OfflineAudioContext`. The expected output comes from JS ports of the native code in `sample_driver_tests.js`:

| What | Native reference |
|---|---|
| Loop modes (disabled, forward, ping-pong, backward) from several start frames — identical samples | `AudioStreamPlaybackWAV::mix()` |
| Volume vector to output channels, stereo / 5.1 / 7.1 | `AudioServer::_mix_step()` channel pairs |
| Volume changes ramp over a mix block; a new bus starts at full volume; stop, pause and a dropped bus fade over a block | `AudioServer::_mix_step()` / `_mix_step_for_channel()` |
| Positions through loops, pitch changes and pause | `AudioStreamPlaybackWAV::get_playback_position()` |
| 3D attenuation high shelf, at and above the cutoff (within 1 dB; Web Audio's shelf slope is fixed) | `AudioFilterSW` `HIGHSHELF` |
| Sends (to an earlier bus goes to Master), solo chains (mute ignored), mute, `move_bus`, `add_bus` | `AudioServer::_mix_step()`, `move_bus()`, `add_bus()` |

## `run_e2e.mjs` — the engine, native against web (needs the editor and web templates)

```bash
./export.sh            # exports/nothreads and exports/threads from bin/
node run_e2e.mjs       # [--only nothreads|threads]
```

`fixture_project/main.gd` is a self-checking suite: finish times and positions through loops, seeks, pitch changes
and pause; Ogg/MP3 samples; polyphony; bus layouts with forward sends and runtime bus edits; 2D, 3D and polyphonic
players; Stream playback through a capture effect, a bus meter, a generator and a randomizer; and the two
limitation warnings below. It ends with `[AUDIO-CHECK] PASS` or `FAIL: <checks>`. `run_e2e.mjs` runs it natively
(headless) and in both exports, and requires the web verdict to pass, every boolean to agree with native, each
limitation warning exactly once on web and none natively, and no other engine warning or console error. The
nothreads export is served without COOP/COEP headers, like most `threads=no` exports are hosted
(`webgpu_notes/TASKS.md` §3 trap 22). The same project is the scene smoketest's `audio_parity` scene.

## What Sample playback still cannot do

Bus **effects** (reverb, EQ, compressor...) do not apply to Sample playbacks, and a long stream played as a
sample is decoded in full on its first `play()`. A sound that needs either must use `playback_type = Stream`,
which mixes through `AudioServer` exactly as on desktop — reliably so in a thread-enabled export, where the
mixing runs on its own thread. Both gaps are warned about: at export (buses with effects) and at runtime, once per
bus and per long stream. Ways to close them, with trade-offs: `webgpu_notes/TASKS.md` §2.32.
