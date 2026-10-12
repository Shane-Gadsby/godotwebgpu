# WebRTC parity tier

Checks that WebRTC behaves the same in a **web export** as it does on
**desktop/mobile**, where it is provided by the `webrtc-native` GDExtension
(libdatachannel) instead of the browser's own `RTCPeerConnection`.

Full evaluation, findings and plan: `webgpu_notes/webrtc_web.md`.
Status summary: `webgpu_notes/TASKS.md` Phase 18.

**There is nothing to implement here.** Web already ships a complete
`WebRTCPeerConnectionJS`; the public API is at 100% parity. This tier exists to
verify that, pin down the places behavior still differs, and keep them from
regressing.

## How it works

One GDScript fixture runs **unchanged on both targets** and prints every
assertion as an `RTC|`-prefixed line. `compare.sh` runs it natively and in a
browser, normalizes only what legitimately varies (object ids, implementation
class names, tick counts, SDP lengths), and diffs. **Any surviving line is a
parity divergence** — no interpretation needed.

Peers connect to each other **in-process**: two `WebRTCPeerConnection`s in one
process with SDP/ICE shuttled directly in GDScript. No signaling server, no
second browser tab, and the whole path is still exercised — JS bridge, DTLS,
SCTP, data channels.

## Running

```bash
# 1. Build the native editor and the web templates (see CLAUDE.md).
# 2. Build the reference peer (once; see webgpu_notes/webrtc_web.md §3):
cd webrtc-native
scons platform=linux target=template_debug arch=x86_64 build_profile=build_profile.json -j$(nproc)
cp -r bin/extension/addons/webrtc_native ../fixtures/loopback_project/addons/

# 3. First scan. Exits 134 with the extension present (F6) — expected, ignore it:
P=fixtures/loopback_project
../../bin/godot.linuxbsd.editor.x86_64 --headless --path $P --import || true

# 4. Export both web variants (fix the absolute template paths in
#    fixtures/loopback_project/export_presets.cfg for your machine first):
../../bin/godot.linuxbsd.editor.x86_64 --headless --path $P \
    --export-release "WebGPU"         "$PWD/exports/loopback/index.html"
../../bin/godot.linuxbsd.editor.x86_64 --headless --path $P \
    --export-release "WebGPU-Threads" "$PWD/exports/loopback_threads/index.html"

# 5. Compare native against web, per fixture:
./compare.sh loopback
./compare.sh mesh
./compare.sh burst
./compare.sh sizes
./compare.sh mesh_close

# A single run, without the native comparison:
RTC_DIR=exports/loopback_threads RTC_TEST=mesh node run_loopback.mjs
```

`compare.sh` takes `RTC_DIR` (which export; default `exports/loopback`) and
`EDITOR_BIN`. The runner serves the export itself with COOP/COEP, which
threaded exports require, and prints `crossOriginIsolated` so a silent failure
is visible.

## Fixtures

| fixture | covers |
|---|---|
| `loopback` | one data channel: states, all nine channel accessors, binary + text round-trips, and F2 (drain after remote close) |
| `mesh` | `WebRTCMultiplayerPeer`: two-peer mesh, `peer_connected`/`peer_disconnected`, all three transfer modes, the three auto-created channels, disconnect detection |
| `burst` | 500 x 1000 bytes in one tick with delayed draining: throughput, ordering, and F3 (backpressure loss) |
| `sizes` | F1: payload sweep 1200 / 1201 / 4 KB / 16 KB / 16385 / 65530 / 65531 / 256 KB — `put_packet()` error, channel state after the send, delivery and integrity |
| `mesh_close` | F2 at the multiplayer layer: packets queued and never drained, then the sender closes. Reads both through `WebRTCMultiplayerPeer` and through a channel `Ref` taken while connected |

Add one as `fixtures/loopback_project/tests/<name>.gd` extending
`tests/rtc_test_base.gd`; the dispatcher picks it up with no further wiring
(`-- <name>` natively, `?test=<name>` on web).

## Rules for new fixtures

1. **Never block the main loop.** `extends Node` and drive from `_process()`.
   A `while` + `OS.delay_msec` loop works natively and can *never* work on web,
   because the JS bridge is callback-driven from the browser event loop — it
   starves and nothing connects.
2. **Print everything as `RTC| key=value`**, stable across runs, so the diff
   stays meaningful. Anything genuinely volatile needs a rule in
   `compare.sh`'s `norm()`.
3. **Call `finish()`** from the base script when done; it signals Playwright on
   web and quits on native.
4. **Do not gate on `get_connection_status()` for a mesh** — it reads
   `CONNECTED` the moment `create_mesh()` succeeds, before any channel is open.
   Gate on `peer_connected`.

## Current result

Native vs web, connected channel: **17 of 18 observables match** (15 before the
F2 fix). The surviving divergences are F5 and F7. `./compare.sh mesh_close` is
**identical**, and `./compare.sh loopback` differs only on F5's and F7's lines.
`threads=yes` is **byte-identical** to nothreads, now across all five
fixtures. `WebRTCMultiplayerPeer` adds no divergence of its own and inherits
only F5.

| id | divergence | severity | state |
|----|------------|----------|-------|
| F1 | `get_max_packet_size()` 1200 web vs 16384 native; web `put_packet()` swallowed send failures | low-medium (not script-exposed) | **FIXED** |
| F2 | buffered packets destroyed on remote close, web only | **high** | **FIXED** |
| F3 | backpressure: web delivers 65/500, native 500/500 | **high** | open |
| F4 | `is_negotiated` wrong JS default | trivial, masked | open |
| F5 | unset `maxRetransmits`/`maxPacketLifeTime`: -1 native vs 0 web | medium | open |
| F6 | `webrtc-native` aborts the editor (exit 134) on first project scan | CI nuisance | open (upstream) |
| F7 | pre-open `get_id()`: 65535 native vs 0 web | low | open |

**F1** was two things, and the behavioral half turned out to be a non-issue:
`sizes` shows Chrome accepting and delivering every payload from 1200 bytes to
256 KB with `put_packet()` returning `OK`, exactly as native does, so the 1200
figure understated the real limit rather than enforcing it. The fix reports
16384 (native's value, RFC-8831 §6.6) and makes `put_packet()` return `FAILED`
when the browser's `send()` throws instead of discarding the exception.

**F2** was two mechanisms, both now gone: `get_packet()`'s `STATE_OPEN` guard
and `close()` wiping the receive buffer. Web now drains undrained packets after
a remote close, as native always did — `loopback` and `mesh_close` agree on
both platforms.

A size sweep divergence remains above **65530 bytes**, but it is F3, not F1:
that is the exact ceiling of the default 64 KB receive `RingBuffer`
(`space_left()` caps at `size - 1`, and each packet costs `p_size + 5`), and
web drops a larger one with `ERROR: Buffer full! Dropping data.` where native
delivers it.

`sizes` also shows `get_buffered_amount()` read immediately after a send:
**web reports the payload, native reports 0**, now across eight payload sizes
up to 256 KB. Still filed as unconfirmed (see `webrtc_web.md` §4.3) — in-process
libdatachannel may genuinely have flushed — but the sweep is a stronger version
of the same observation.

Not yet covered: `MultiplayerAPI`/`@rpc`, `dlink_enabled=yes`, and
Firefox/Safari.
