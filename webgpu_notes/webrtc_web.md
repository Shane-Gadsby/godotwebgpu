# WebRTC on the Web Target — Feasibility Evaluation and Hardening Plan

**Status:** Evaluation complete. Phase 0 **done**, the `WebRTCMultiplayerPeer`
layer **done and at parity but for F5**, **`threads=yes` done and at full
parity**, F3 **quantified**, Phase 2 dependency built and verified. F2, F3 and
F5 empirically proven on both platforms. Remaining: the fixes themselves, F1's
behavioural test, F2 at the multiplayer layer, `MultiplayerAPI`/`@rpc`, and the
Firefox/Safari matrix.
**Date:** 2026-10-12 (findings confirmed against the reference implementation same day)
**Branch at time of writing:** `webgpu-4.8` (`c0828b3488`)
**Scope:** Bring web-export WebRTC to verified parity with the desktop/mobile
implementation, and document every place parity is not achievable.

---

## 1. Executive summary

**WebRTC data channels are already implemented, already compiled into this
fork's web templates, and already at 100% public-API parity with
desktop/mobile.** There is no feature to add. The work that remains is
verification, a handful of bug fixes in inherited upstream code, and
documentation of the browser-imposed limits.

`modules/webrtc/` is platform-agnostic: its `config.py` `can_build()` returns
`True` unconditionally, so the module compiles for web exactly as it does
everywhere else. On web, `WebRTCPeerConnection::create()` returns a
`WebRTCPeerConnectionJS` (`modules/webrtc/webrtc_peer_connection.cpp:48-51`),
a thin C++ wrapper over the browser's own `RTCPeerConnection`, bridged by the
502-line `modules/webrtc/library_godot_webrtc.js`.

That JS-backed implementation overrides **every** pure virtual on both base
classes — all three peer-connection state getters, offer/answer/SDP/ICE, and
all nine `WebRTCDataChannel` accessors. Peer and channel configuration
dictionaries are passed straight through as JSON to `new RTCPeerConnection()`
and `createDataChannel()` (`library_godot_webrtc.js:378,490-492`), so ICE/TURN
server configuration and channel options have no lossy translation layer.

The desktop/mobile implementation being compared against — the official
`webrtc-native` GDExtension, built on libdatachannel — implements the same two
extension classes and exposes **no additional API surface**. Hence the parity
claim above.

### Verified present in the existing build artifacts

Not just in source — confirmed in `bin/` as built on 2026-10-07:

- `WebRTCPeerConnectionJS`, `WebRTCDataChannelJS` and `WebRTCMultiplayerPeer`
  symbols present in every `bin/godot.side.web.template_*.wasm`
  (release + debug, threads + nothreads, dlink).
- `godot_js_rtc_pc_create` and `GodotRTCDataChannel` present in every
  `bin/godot.web.template_*.js` (including the `.wrapped.js` variants).

### Feasibility verdicts for the three readings of "native WebRTC"

| Reading | Verdict |
|---|---|
| Make WebRTC work in web exports at parity with desktop | **Already done.** Remaining effort is a test harness plus small fixes, ~8-11 days. This is the plan below. |
| Compile a real native stack (libdatachannel) to WASM so web runs the same C++ path | **Infeasible — hard platform wall, not an effort question.** ICE requires raw UDP, which browsers do not expose at all; DTLS-SRTP and SCTP are likewise locked behind the browser's own implementation. WebTransport and WebSockets cannot substitute for ICE/STUN/TURN negotiation. |
| Add media streaming (camera/mic/video tracks) | Feasible **web-only** (the browser does the heavy lifting; the work is Godot-side API design plus `AudioStream`/`Texture` plumbing), but Godot supports this on *no* platform, so it is a new feature rather than parity. Cross-platform parity for it would be a very large project. |

---

## 2. Confirmed parity divergences

Originally found by reading the web implementation, then **confirmed against the
reference implementation** (`webgpu_tests/webrtc_parity/webrtc-native/src/WebRTCLibDataChannel.cpp`)
once it was on disk and building — see §3. All five are inherited from upstream
Godot; **none are fork damage**, and all fixes are upstreamable to
`godotengine/godot`.

Web side: `modules/webrtc/webrtc_data_channel_js.cpp` and
`modules/webrtc/library_godot_webrtc.js`.
Native side: `src/WebRTCLibDataChannel.cpp` in webrtc-native.

### F1 — `get_max_packet_size()`: 1200 (web) vs 16384 (native) — CONFIRMED

- Web: `return 1200;` (hardcoded, `webrtc_data_channel_js.cpp:143`).
- Native: `return 16384;` with an explicit citation of RFC-8831 §6.6
  (`WebRTCLibDataChannel.cpp:223`).

A 13.6x divergence. **Severity revised down to low-medium** after a second
finding: `get_max_packet_size` is **not bound to GDScript** — it is a
`PacketPeer` C++ virtual, and `WebRTCDataChannel::_bind_methods()`
(`modules/webrtc/webrtc_data_channel.cpp:36-51`) does not expose it. So this
only affects C++ consumers of `PacketPeer::get_max_packet_size()`, not user
scripts, and the harness **cannot assert it from GDScript** — it needs either a
C++-side check or a behavioural test (send 1201..16384 bytes and observe).

Separately, the web `put_packet()` enforces no limit at all
(`webrtc_data_channel_js.cpp:135-141`), passing any size to `ref.send()`, so an
oversized send fails inside JS with no `Error` returned. Native validates
(`!channel`, `channel->isClosed()`) and catches `std::exception` to return
`FAILED` with the message printed.

### F2 — Buffered packets destroyed on remote close (web only) — CONFIRMED

- Web: `get_packet()` hard-fails unless state is `STATE_OPEN`
  (`ERR_FAIL_COND_V(get_ready_state() != STATE_OPEN, ERR_UNCONFIGURED)`, `:109`),
  and `_on_close` -> `close()` -> `in_buffer.resize(0)` (`:63,86`) wipes the
  queue.
- Native: `_get_packet()` has **no state guard whatsoever** — it fails only when
  the queue is empty (`WebRTCLibDataChannel.cpp:185-198`). Buffered packets
  remain drainable after the channel closes.

**PROVEN ON BOTH PLATFORMS** by the shared loopback fixture (§4) — identical
GDScript, one packet queued and deliberately not drained before the sender
closes:

```
native:  f2 ch2_closed_after=2 ticks avail_after_close=1
         f2 drain_after_close len=10 content='f2-pending'   -> verdict=DRAINED
web:     f2 ch2_closed_after=1 ticks avail_after_close=0
         ERROR: Condition "get_ready_state() != STATE_OPEN" is true.
                Returning: ERR_UNCONFIGURED
         f2 drain_after_close len=0 content=''              -> verdict=LOST
```

The highest-severity finding, now beyond doubt: on web the last packets before a
disconnect are silently lost; on desktop they are delivered. Note
`avail_after_close=0` on web — the count is zeroed too, so **both** mechanisms
fire: the `STATE_OPEN` guard *and* the buffer wipe. A fix must relax the guard
*and* stop `close()` from clearing the buffer, which has ordering implications
for the `_on_close`/`_on_error` paths that both call `close()`.

### F3 — Silent drop vs unbounded queue on backpressure — CONFIRMED

- Web: fixed `RingBuffer` sized from
  `network/limits/webrtc/max_channel_in_buffer_kb` (default 64 KB), and
  `_on_message` drops the packet when it is full
  (`ERR_FAIL_COND_MSG(in_buffer.space_left() < (int)(p_size + 5), "Buffer full! Dropping data.")`, `:78`).
- Native: an unbounded `std::queue` (`shared_state->packet_queue`), which never
  drops.

So a reliable-ordered channel — contractually lossless — **can lose data on web
but not on desktop** if the application polls slowly. The two implementations
have made opposite trade-offs: web bounds memory and sacrifices the delivery
guarantee; native honours the guarantee and lets memory grow. Neither is
obviously wrong, but they must not silently differ.

**QUANTIFIED** by `tests/burst.gd` (§4.2) — 500 packets of 1000 bytes sent in a
single tick on a reliable-ordered channel, receiver left to accumulate for 120
ticks before draining:

| | native | web (both threads and nothreads) |
|---|---|---|
| `put_packet` errors | 0 | 0 |
| delivered | **500 / 500** | **65 / 500** |
| dropped | 0 | **435** |
| `out_of_order` / `first_gap` | 0 / none | 0 / none |
| verdict | LOSSLESS | **LOSSY** |

65 packets x 1005 bytes ≈ 65,325 bytes, i.e. exactly the 64 KB buffer, so the
measurement matches the code precisely.

Two details that matter for diagnosis, and that sharpen the earlier wording in
this doc:

- **It is a clean tail-drop, not scattered loss.** The receiver got sequence
  0..64 contiguously and then nothing — `out_of_order=0` and no gap *within*
  what arrived. An application therefore **cannot** detect it by looking for
  sequence gaps in the delivered stream; the stream simply stops.
- **There is no API-level signal on either side.** All 500 `put_packet()` calls
  returned `OK`, and nothing reaches the receiving script. It is not completely
  invisible, though: the engine prints `ERROR: Buffer full! Dropping data.`
  **once per dropped packet** — 435 console errors in this run, which is its own
  problem at volume.

### F4 — `is_negotiated` wrong default (web) — CONFIRMED COSMETIC

`library_godot_webrtc.js:170-174` passes `65535` as the fallback for a boolean
getter (copy-pasted from `id_get` directly above), where the neighbouring
`is_ordered` correctly passes `true`. Native stores and returns a real bool
(`WebRTCLibDataChannel.cpp:164-167`).

Every current browser exposes `negotiated` as a real boolean, so the fallback
never fires. Latent, not live. One-line fix.

### F5 — Unset reliability values: -1 (native) vs 0 (web) — CONFIRMED, value corrected

Found by reading, then **measured — and the measurement corrected the predicted
value.** The original read of this doc said web returns `65535`. It does not; it
returns **0**.

- Native returns **-1** for both when unset: `maxRetransmits.value_or(-1)` and
  `maxPacketLifeTime.has_value() ? ... : -1`
  (`WebRTCLibDataChannel.cpp:149-157`).
- Web returns **0** for both, measured in a real browser (§4):
  native `lifetime=-1 rtx=-1` vs web `lifetime=0 rtx=0`.

**Why the `65535` defaults in the JS are dead code.** `get_prop` falls back only
when the property is `undefined`:

```js
get_prop: function (p_id, p_prop, p_def) {
    const ref = IDHandler.get(p_id);
    return (ref && ref[p_prop] !== undefined) ? ref[p_prop] : p_def;
},
```
(`library_godot_webrtc.js:87-90`)

But browsers expose `maxRetransmits` and `maxPacketLifeTime` as **`null`**, not
`undefined`, when unset. `null !== undefined`, so the `65535` default never
fires; `null` passes through the `'ii'` signature and arrives in C++ as **0**.
The same mechanism explains why F4's wrong `65535` default is harmless, and is a
reminder that the JS fallback values in this file are largely unreachable and
cannot be trusted as documentation of behaviour.

Both values *are* script-exposed (`get_max_retransmits()`,
`get_max_packet_life_time()`), so unlike F1 this is directly visible to user
code: GDScript branching on these sees `0` on web and `-1` on desktop. Cheapest
real fix in the list, and the most likely to be silently relied upon.

**Severity raised after the multiplayer-layer test (§4):** F5 is the *only*
divergence that reaches `WebRTCMultiplayerPeer`, and it shows on all three
channels it auto-creates for every peer — so it is not a corner case of
hand-rolled channels, it is present in every project that uses the multiplayer
peer.

### F6 — webrtc-native aborts the editor on first project scan — CONFIRMED (new)

Not a parity issue; a harness/CI blocker found while setting up the reference
peer (§3).

With the extension present, whichever command performs the **first** filesystem
scan of a project aborts at shutdown with **exit 134** (SIGABRT, no diagnostic
printed):

| invocation | exit |
|---|---|
| `--headless --import` (first scan, extension present) | **134** |
| `--headless --import` (first scan, no extension) | 0 |
| `--headless --import` (second run, extension present) | 0 |
| `--headless --editor --quit` (first scan, extension present) | **134** |
| `--headless --path <proj>` (running the game) | 0 |
| `--headless --export-release` | 0 |

`extension_list.cfg` **is** written correctly before the abort, and every later
invocation exits 0, so it is a shutdown-only crash. It still breaks any CI step
that checks exit status. Likely libdatachannel/mbedtls global teardown or
GDExtension unload ordering — upstream's HEAD includes a `fix/threading` merge,
so this area is actively churning.

**Harness workaround:** run the first scan as a throwaway step and ignore its
exit code, then run the real command. Worth reporting upstream with this table.

### F7 — Pre-open `get_id()`: 65535 (native) vs 0 (web) — CONFIRMED (new)

Measured (§4): before a channel opens, native reports `id=65535` and web reports
`id=0`. Same `null`-vs-`undefined` mechanism as F5. **Post-open both report
`id=1`**, so this affects only code that reads `get_id()` before the channel is
open. Low severity, but it invalidates the note in the §3 baseline table that
claimed pre-open `id` agreed across platforms — that comparison was against the
JS *default*, not the measured value.

---

## 3. Build verification (2026-10-12)

**Result: webrtc-native builds cleanly and loads successfully in this fork's
4.8 dev engine. The Phase 2 dependency risk is eliminated.**

### Location

`webgpu_tests/webrtc_parity/webrtc-native/` — moved there as the Phase 2
reference-peer dependency of the parity tier. It is a **separate git clone with
its own `.git`, deliberately not vendored and not a submodule**, and is
gitignored via `webgpu_tests/webrtc_parity/.gitignore`. ~581 MB on disk with
submodules and build output (123 MB godot-cpp, 150 MB thirdparty).

Upstream state: `62135fc`, submodules fully initialized recursively
(libdatachannel v0.24.6, mbedtls 3.6.7, libjuice 1.7.4, libsrtp 2.8.0,
usrsctp, plog, json).

### Why the version risk evaporated

`misc/cfg.gdextension` declares `compatibility_minimum = 4.3`, and the
SConstruct defaults `api_version` to `4.3`
(`SConstruct:56-58`). godot-cpp bundles `extension_api-4-3.json` through
`extension_api-4-7.json` — **no 4.8 file**, which initially looked like the
blocker. It is not: GDExtension is backward-compatible, so an extension built
against the 4.3 API loads in 4.8 unchanged. **No custom `--dump-extension-api`
file is needed**, and neither fallback from the original plan (build from
source against 4.8 godot-cpp; substitute `node-datachannel`) is required.

### Host dependencies (all already present)

cmake 3.28.3, gcc/g++, scons. The build drives cmake to build mbedtls and
libdatachannel as static libraries (`tools/cmake.py`, `tools/mbedtls.py`,
`tools/rtc.py`), then links them into the extension. `disable_exceptions=no` is
forced by the SConstruct because libdatachannel requires exceptions.

### Build commands (both verified, exit 0)

```bash
cd webgpu_tests/webrtc_parity/webrtc-native
scons platform=linux target=template_debug   arch=x86_64 build_profile=build_profile.json -j$(nproc)
scons platform=linux target=template_release arch=x86_64 build_profile=build_profile.json -j$(nproc)
```

Note `platform=linux`, **not** `linuxbsd` — this is godot-cpp's platform
naming, not the engine's. Artifacts (~4.57 MB each):

```
bin/extension/addons/webrtc_native/webrtc_native.gdextension
bin/extension/addons/webrtc_native/lib/libwebrtc_native.linux.template_debug.x86_64.so
bin/extension/addons/webrtc_native/lib/libwebrtc_native.linux.template_release.x86_64.so
```

`build_profile=build_profile.json` (as upstream CI uses) trims the godot-cpp
binding generation to the handful of classes the extension needs and is a large
build-time saving — the first build here was run without it and compiled the
entire godot-cpp binding set unnecessarily. Always pass it.

### Load verification against `bin/godot.linuxbsd.editor.x86_64` (4.8.dev, de44b9719)

**Gotcha that cost a false negative:** a fresh project has no
`.godot/extension_list.cfg`, and `--headless -s script.gd` does **not** generate
one, so the extension is silently not loaded and
`WebRTCPeerConnection.create()` falls back to the base
`WebRTCPeerConnectionExtension` with
`WARN_PRINT_ONCE("No default WebRTC extension configured.")`. The harness must
run an import pass first:

```bash
godot --headless --path <project> --import     # generates .godot/extension_list.cfg
godot --headless --path <project> -s test.gd
```

After the import pass, confirmed working:

- `WebRTCPeerConnection.new().get_class()` -> `WebRTCLibPeerConnection`
  (i.e. the extension's `register_types` ran and called
  `set_default_extension`)
- `create_data_channel()` -> `WebRTCLibDataChannel`
- `initialize({})` -> `0` (OK)
- Clean process exit with peers created, both with and without explicit
  `close()`.

### Native baseline (seed for the Phase 2 parity table)

Captured from the five planned channel configs. **Caveat: these are pre-open
values** (`ready=0`, i.e. `STATE_CONNECTING`) with no connected remote peer, so
`id` in particular will change once a channel actually opens. The real table
needs a connected pair; this is only a baseline for the static properties.

| config | ordered | id | lifetime | retransmits | protocol | negotiated |
|---|---|---|---|---|---|---|
| `{}` | true | 65535 | -1 | -1 | `''` | false |
| `{ordered: false, maxRetransmits: 0}` | false | 65535 | -1 | 0 | `''` | false |
| `{ordered: false, maxPacketLifeTime: 300}` | false | 65535 | 300 | -1 | `''` | false |
| `{negotiated: true, id: 7}` | true | 7 | -1 | -1 | `''` | true |
| `{protocol: "godot-test"}` | true | 65535 | -1 | -1 | `godot-test` | false |

Note the pre-open `id` of 65535 matches the web JS *fallback* of 65535 — but
the web implementation never reaches that fallback and measures **0** instead.
See F7; this table's comparison against the JS default was misleading.

### Upstream's own position on web, confirmed

HEAD commit `d23b476` ("Add exclude_tags for web, no-webrtc", Fabio
Alessandrelli, 2026-07-20) adds `exclude_tags = ["web", "no-webrtc"]` to
`misc/cfg.gdextension`, with the message: *"Prevents the export process from
complaining about the missing gdextension library on web (4.8+)."*

This is authoritative confirmation of §1 from the WebRTC maintainer: there is
**no web build of the native extension and none is planned**, web uses the
built-in JS implementation, and 4.8 is precisely the version where the export
process needed teaching not to ask. Upstream CI
(`.github/workflows/build_release.yml`) builds android, ios, linux, macos and
windows — **no web target**.

One loose end, noted for completeness and not a path forward:
`tools/cmake.py:187` does contain an `emcmake` branch for
`platform == "web"`. It is generic shared build tooling, there is no web entry
in `cfg.gdextension` to consume it, and it does not change the §1 verdict that
libdatachannel cannot work in a browser (no raw UDP).

---

## 4. Phase 0 baseline — COMPLETE, both halves green

**Result: WebRTC works end-to-end in this fork's web export.** Connection
establishment, SDP exchange, ICE, data-channel open, and both binary and text
round-trips all pass in Chromium against a current `nothreads` template. The
plan does not need reordering.

### Method: in-process loopback, no signaling server

Rather than a two-tab setup, the fixture creates **two peers in one process**
and shuttles SDP/ICE between them directly in GDScript. No signaling server, no
second page, and it still exercises the entire path — JS bridge, DTLS, SCTP,
data channels. Crucially, **the same file runs on native and on web**, so the
`RTC|`-prefixed output lines can be diffed directly to produce the parity table.

Assets created:

| path | role |
|---|---|
| `webgpu_tests/webrtc_parity/fixtures/loopback_project/rtc_loopback.gd` | the fixture — single source of truth, runs on both targets |
| `webgpu_tests/webrtc_parity/fixtures/loopback_project/` | project wrapping it: `main.tscn`, `project.godot`, `export_presets.cfg`, `addons/webrtc_native/` |
| `webgpu_tests/webrtc_parity/fixtures/loopback_project/main.gd` | dispatcher — picks the fixture from `-- <name>` (native) or `?test=<name>` (web) |
| `webgpu_tests/webrtc_parity/fixtures/loopback_project/tests/rtc_test_base.gd` | shared base: `out()`, `finish()`, tick/phase state |
| `webgpu_tests/webrtc_parity/fixtures/loopback_project/tests/loopback.gd` | the channel-level loopback |
| `webgpu_tests/webrtc_parity/fixtures/loopback_project/tests/mesh.gd` | the `WebRTCMultiplayerPeer` mesh |
| `webgpu_tests/webrtc_parity/fixtures/loopback_project/tests/burst.gd` | throughput/ordering/backpressure stress; measures F3 |
| `webgpu_tests/webrtc_parity/exports/loopback_threads/` | the `threads=yes` export (gitignored) |
| `webgpu_tests/webrtc_parity/run_loopback.mjs` | Playwright runner, scrapes `RTC\|` console lines |
| `webgpu_tests/webrtc_parity/compare.sh` | runs one fixture both ways, normalizes, diffs — any surviving line is a divergence |
| `webgpu_tests/webrtc_parity/exports/loopback/` | the web export (gitignored) |

The project carries the native extension in `addons/` *and* exports to web
cleanly, so **one project serves both targets** — the `exclude_tags` from
`d23b476` (§3) means the web export does not complain about the missing native
library. Verified: `--export-release "WebGPU"` exits 0 with no such warning,
which corroborates that upstream fix working in our 4.8 build.

### Harness design constraint discovered the hard way

The fixture was first written as a `SceneTree` script with a blocking
`while` loop calling `OS.delay_msec(10)`. **That form works on native and can
never work on web.** The entire JS bridge is callback-driven from the browser
event loop (ICE candidates, SDP, `onmessage`), and a blocking loop on a
single-threaded web build never yields, so no callback ever fires and the test
times out with no connection.

The portable form, now used, is `extends Node` with the poll/step logic in
`_process()`. **Every fixture in Phases 1-3 must follow it.** Completion is
signalled to Playwright by setting `window.__rtc_done` through
`JavaScriptBridge.eval()` on web, and `get_tree().quit()` on native.

### Reproduce

```bash
# native (first scan aborts with 134 — see F6 — so run it, ignore it, continue)
P=webgpu_tests/webrtc_parity/fixtures/loopback_project
./bin/godot.linuxbsd.editor.x86_64 --headless --path $P --import || true
./bin/godot.linuxbsd.editor.x86_64 --headless --path $P

# web
./bin/godot.linuxbsd.editor.x86_64 --headless --path $P \
    --export-release "WebGPU" "$PWD/webgpu_tests/webrtc_parity/exports/loopback/index.html"
cd webgpu_tests/webrtc_parity/exports/loopback && python3 -m http.server 8099 &
cd webgpu_tests/webrtc_parity && node run_loopback.mjs
```

`export_presets.cfg` hardcodes absolute `custom_template/*` paths to
`bin/godot.web.template_*.wasm32.nothreads.zip`, as every other preset in
`webgpu_tests/` does, so it needs fixing up per machine. `run_loopback.mjs`
borrows Playwright 1.63.0 from `webgpu_tests/screenshot_comparison/node_modules`
rather than adding a third copy; give the tier its own `package.json` when it
graduates into `local_ci.sh`.

### Measured parity table (connected channel, `{"ordered": true}`)

| observable | native | web | verdict |
|---|---|---|---|
| implementation class | `WebRTCLibPeerConnection` / `WebRTCLibDataChannel` | `WebRTCPeerConnectionJS` / `WebRTCDataChannelJS` | expected |
| `initialize({})` | 0 | 0 | match |
| offer / answer SDP length | 444 / 444 | 458 / 457 | match (cosmetic) |
| ticks to both channels open | 2-4 | 3 | match |
| `get_label()` | `loopback` | `loopback` | match |
| `get_ready_state()` open | 1 | 1 | match |
| `is_ordered()` | true | true | match |
| `get_id()` pre-open | 65535 | **0** | **F7** |
| `get_id()` post-open | 1 | 1 | match |
| `get_max_packet_life_time()` unset | -1 | **0** | **F5** |
| `get_max_retransmits()` unset | -1 | **0** | **F5** |
| `get_protocol()` | `''` | `''` | match |
| `is_negotiated()` | false | false | match |
| `get_buffered_amount()` | 0 | 0 | match |
| `get_write_mode()` default | 1 (BINARY) | 1 (BINARY) | match |
| binary round-trip + `was_string_packet()` | ok, false | ok, false | match |
| text round-trip + `was_string_packet()` | ok, true | ok, true | match |
| drain after remote close | **DRAINED** | **LOST** | **F2** |

Everything not flagged matches. The three flagged rows are F2, F5 and F7.
F1 and F3 are not in this table: F1 is not script-exposed (needs a behavioural
or C++-side test) and F3 needs the deliberate-backpressure scenario from
Phase 3.

### `WebRTCMultiplayerPeer` layer: at parity, inheriting only F5

The layer real projects actually use, tested with a two-peer mesh
(`tests/mesh.gd`) — the same in-process technique, both `WebRTCMultiplayerPeer`
instances and both peer connections in one process.

`./compare.sh mesh` produces **32 output lines on each side, and after
normalizing object ids, implementation class names, tick counts and SDP
lengths the only surviving difference is F5**, on all six channels:

```
-RTC| mp1 ch0 ready=1 ordered=true id=1 lifetime=-1 rtx=-1 negotiated=true label=reliable
+RTC| mp1 ch0 ready=1 ordered=true id=1 lifetime=0  rtx=0  negotiated=true label=reliable
```

Identical across both platforms: `get_unique_id()`, `create_mesh()`,
`add_peer()`, `peer_connected`/`peer_disconnected` (both sides, correct ids),
`get_connection_status()`, `get_peers()` contents and its emptying after a
disconnect, `connected=true` per peer, the three auto-created channels
(labels `reliable`/`ordered`/`unreliable`, ids 1/2/3, `negotiated=true`,
`ordered` flags matching per channel), **all three transfer modes delivering
with the correct `get_packet_peer()`**, and disconnect detection within one
tick of `close()`.

**Conclusion: the multiplayer layer adds no divergences of its own.** That
raises F5's practical severity though — it is not a channel-layer curiosity, it
shows on the channels `WebRTCMultiplayerPeer` builds for every peer in every
project.

Two honest gaps in this result:

- **F2 is not reproduced at the multiplayer layer and is therefore untested
  there.** The mesh fixture polls every tick, so no backlog ever exists for a
  close to destroy. Whether a real project loses final packets *through*
  `WebRTCMultiplayerPeer` needs a deliberate no-poll-then-close variant.
- `MultiplayerAPI`-level RPC (`SceneMultiplayer`, node paths, `@rpc`) is still
  untested; this covers the `MultiplayerPeer` packet/signal contract beneath it.

**Fixture trap worth keeping:** for a mesh, `get_connection_status()` returns
`CONNECTION_CONNECTED` the moment `create_mesh()` succeeds — it says nothing
about whether any remote peer's channels are open. Gating the first send on it
raced the channels and failed with `ERROR: DataChannel not open` / `err=1`.
Gate on `peer_connected` instead.

### §4.2 `threads=yes`: full parity, and F3 quantified

**Result: WebRTC under `threads=yes` is byte-identical to `nothreads`.** The
`__proxy: 'sync'` marshalling that every function in `library_godot_webrtc.js`
carries — never exercised in this fork before now — works, including under a
500-packet burst.

Setup: a second export preset `WebGPU-Threads` in the fixture project
(`variant/thread_support=true`, pointing at `bin/godot.web.template_*.wasm32.zip`,
the threaded non-dlink templates), exported to `exports/loopback_threads/`.
**No Emscripten patches were needed** — `misc/emsdk_patches/` is required for
`threads=yes dlink_enabled=yes`, and this is threads *without* dlink.

Both variants are proven genuinely different builds rather than an accidental
re-run of the same one: the runner reports `crossOriginIsolated=true`, and the
dispatcher prints `OS.has_feature("threads")` (the tag `web_main.cpp:122` itself
checks), which reads `true` on the threaded export and `false` on the other.

All three fixtures, threads vs nothreads, after normalizing object ids, tick
counts and the variant tag itself:

| fixture | lines each side | result |
|---|---|---|
| `loopback` | 19 | **IDENTICAL** |
| `mesh` | 33 | **IDENTICAL** |
| `burst` | 9 | **IDENTICAL** |

So `threads=yes` adds no divergence of its own, and **F3 reproduces identically
under it** (65/500 delivered either way) — the backpressure loss is a property
of the fixed `RingBuffer`, not of threading.

What this does *not* cover: concurrent access to a channel or multiplayer peer
from several Godot threads. Godot's `MultiplayerPeer` is not documented as
thread-safe and the fixtures drive everything from the main loop, so that
remains out of scope rather than verified.

**Harness changes this phase.** `run_loopback.mjs` now serves the export itself
with `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`, which a threaded export requires
for `SharedArrayBuffer`. This removes the external `python3 -m http.server`
dependency and, for this tier, closes the gap HANDOFF §4.13 describes — the
runner cannot be pointed at a non-isolating server by accident, and it prints
`crossOriginIsolated` so a silent failure is visible. `RTC_DIR` selects the
export, `RTC_TEST` the fixture.

### §4.3 A `get_buffered_amount()` divergence worth a proper test (unconfirmed)

The burst run also showed `buffered_after_send=500000` on web against **0** on
native, read immediately after the 500 sends. If real, it matters: a project
using `get_buffered_amount()` for flow control would see wildly different
numbers per platform.

**Not recorded as a finding, because the comparison is not sound as measured.**
Native is libdatachannel over in-process loopback and may legitimately have
flushed all 500 KB before the read, so `0` could be accurate rather than wrong.
Web reports the browser's `bufferedAmount`, which is also accurate for its own
state. Settling it needs a timing-controlled test — read the value at a fixed
point while a known quantity is deliberately in flight — rather than this
incidental read.

---

## 5. Hardening plan

New test tier at `webgpu_tests/webrtc_parity/`, following the established
per-tier shape (`package.json` + `run_tests.mjs` + Playwright, as in
`webgpu_tests/resource_lifecycle/` and `webgpu_tests/screenshot_comparison/`).

### Phase 0 — Baseline — COMPLETE

Both halves done; see §3 (native reference peer builds and loads) and §4 (web
export works end-to-end, with a measured parity table). No reordering needed.

### Phase 1 — Harness and web-to-web tier (2-3 days)

- **Signaling server.** Node `ws` server living in the tier directory. Clients
  talk to it using Godot's own `WebSocketPeer`, which is JS-backed on web
  (`modules/websocket/emws_peer.cpp`) and native on desktop
  (`modules/websocket/wsl_peer.cpp`) — so one GDScript signaling client runs
  identically on both sides and the signaling layer never becomes a confound.
- **Fixture project.** A Godot project under `webgpu_tests/webrtc_parity/fixtures/`
  whose GDScript drives the connection and dumps every assertion as structured
  JSON to the console, which Playwright scrapes. Mirrors how `scene_smoketest`
  reads results out of the page.
- **Tests.** Two Playwright pages, offerer and answerer:
  - Full state-machine progression across all three enums
    (`ConnectionState`, `GatheringState`, `SignalingState`).
  - Channel-property round-trips across five configs: reliable-ordered;
    unordered + `maxRetransmits: 0`; unordered + `maxPacketLifeTime`;
    `negotiated` + explicit `id`; custom `protocol`/`label`.
  - Payload round-trips in both `WRITE_MODE_TEXT` and `WRITE_MODE_BINARY`,
    with `was_string_packet()` asserted.
  - `WebRTCMultiplayerPeer` 3-peer mesh plus a `MultiplayerAPI` RPC round-trip,
    since that is how real projects actually consume this.

Deliverable: a green web-to-web tier. This alone characterises F1-F4 as
*behaviours*, independently of whether desktop agrees.

### Phase 2 — Desktop reference peer and the parity table (2-3 days)

The phase that actually answers "parity." Carries the plan's one real external
dependency.

- **Dependency: already satisfied — see §3.** Both `template_debug` and
  `template_release` linux x86_64 builds exist and are verified to load in the
  4.8 dev engine. The original risk (no 4.8 `extension_api.json`) proved to be a
  non-issue, and neither documented fallback is needed. The harness must
  remember the `--import` pass before `-s` (§3), or the extension silently will
  not load.
- Run the *same* fixture GDScript on the native editor/template as one peer and
  in the browser as the other, in **both** directions (web offers / native
  offers).
- Emit a **parity table**: every one of the ~12 API observables, web value vs
  native value, per channel config. Any divergence is either a bug to fix or a
  documented browser-imposed limit — there is no third category. **This table
  is the actual deliverable of the whole project.**

### Phase 3 — Edge cases and the build matrix (2 days)

- **Backpressure** (F3): push past `max_channel_in_buffer_kb` without polling;
  assert whether data loss occurs and whether it is reported.
- **Close semantics** (F2): close mid-transfer with packets in flight; assert
  drainability.
- **Size limits** (F1): sweep payload sizes across 1200 / 16 KB / 64 KB /
  256 KB; assert the error surfaced to GDScript.
- **Leak check**: 50 connect/disconnect cycles, watching `IDHandler` growth via
  `evaluate_script` and the wasm heap.
- **Browser matrix**: Chrome, Firefox, Safari. Non-optional — Firefox has
  already produced two fork-specific blockers (`TASKS.md` Tasks 38, 41) and its
  WebRTC stack diverges from Chrome's more than its WebGPU one does.
- **Build matrix**: `threads=no` and `threads=yes` are **DONE — identical, see
  §4.2**; `dlink` remains untested (it needs the `misc/emsdk_patches/` toolchain
  patches, unlike threads-without-dlink).

### Phase 4 — Fix, integrate, document (2-3 days)

- Fix what Phases 2-3 confirm. Expect the fixes to be small and local to
  `webrtc_data_channel_js.cpp` and `library_godot_webrtc.js`; F2 and F3 are the
  ones with real design content.
- Keep upstreamable commits (anything in the shared or JS files) separate from
  fork-specific ones — F1-F5 and F7 are upstream Godot bugs, and F6 is an
  upstream webrtc-native bug worth reporting with the table in §2.
- Wire the tier into `webgpu_tests/local_ci.sh` as a Stage-2 standalone entry
  (it needs no engine rebuild once exports exist), and into the web CI workflow.
- Add `webgpu_tests/webrtc_parity/README.md` holding the parity table, and a
  `TASKS.md` phase entry in the existing
  Status/Severity/Lines/Issue/Investigation format.

### Total: ~5-7 working days (revised down from 8-11, then 7-9)

Phase 0 is complete, Phase 2's dependency risk is retired (§3), the shared
fixture pattern and Playwright runner already exist and work on both targets
(§4), and seven divergences are confirmed — two of them (F2, F5) already proven
end-to-end with reproducible output on both platforms. The harness now has to
*verify and regression-guard known* divergences and extend coverage to F1/F3,
rather than hunt for unknown ones.

Both of the unknowns named here in the previous revision are now settled:
`threads=yes` is at full parity (§4.2) and the `WebRTCMultiplayerPeer` layer
adds no divergences of its own (§4). What is left is deciding and making the
fixes, F1's behavioural test, F2 at the multiplayer layer,
`MultiplayerAPI`/`@rpc`, the dlink variant, and the Firefox/Safari matrix.

---

## 6. Explicitly out of scope

- **`set_default_extension()` stays a no-op on web.**
  `webrtc_peer_connection.cpp:49-51` hardcodes the JS implementation under
  `WEB_ENABLED`, so the default-extension mechanism is dead code there. This is
  *correct* behaviour — there is no web build of `webrtc-native` to load, and
  the JS implementation is what you want regardless. It needs one documented
  line so nobody debugs it twice.
- **Media tracks (audio/video).** See §1.
- **A libdatachannel-in-WASM port.** See §1.
- **Browser-imposed limits that cannot be closed**: no DTLS certificate or
  fingerprint control, no local port-range control, no mDNS candidate control.
  None of these are exposed in Godot's API on any platform anyway, so they do
  not affect parity — they only need recording in the parity table as
  "N/A both sides."
