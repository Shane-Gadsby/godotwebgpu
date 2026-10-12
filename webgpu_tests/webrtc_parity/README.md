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
| `grow` | F3 regression guard: three grow / partial-drain / regrow rounds, 2100 packets, checking loss, duplication and reordering — specifically growing a **wrapped** receive buffer |
| `buffered` | `get_buffered_amount()` under a 10 MB one-tick burst, sampling sender and receiver together: asserts range and settling, records the per-platform curve |
| `chancfg` | channel-config round-trips: baseline, the `maxPacketLifetime` typo, the correct `maxPacketLifeTime`, and `maxRetransmits: 0` — every accessor read back on both ends (F8) |
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
   `compare.sh`'s `norm()` — or, for a number that is legitimately
   platform- and run-dependent, prefix the line `curve`, which `norm()` already
   collapses while the raw output keeps the value.
3. **Call `finish()`** from the base script when done; it signals Playwright on
   web and quits on native.
4. **Do not gate on `get_connection_status()` for a mesh** — it reads
   `CONNECTED` the moment `create_mesh()` succeeds, before any channel is open.
   Gate on `peer_connected`.

## Current result

**All eight fixtures compare identical, on both web variants, and 18 of 18
channel observables match** (15 on the first pass). Every confirmed divergence
is fixed: F1-F5, F7 and F8. Only F6 is left, and it is an upstream
webrtc-native nuisance affecting this harness rather than the engine.

A green sweep means the two implementations *agree*, which is not the same as
either being right — **F8 was a bug both platforms shared, so the diff stayed
clean for the whole first round of this work**. See the note above.

| id | divergence | severity | state |
|----|------------|----------|-------|
| F1 | `get_max_packet_size()` 1200 web vs 16384 native; web `put_packet()` swallowed send failures | low-medium (not script-exposed) | **FIXED** |
| F2 | buffered packets destroyed on remote close, web only | **high** | **FIXED** |
| F3 | backpressure: web delivered 65/500 where native delivers 500/500 | **high** | **FIXED** |
| F4 | `is_negotiated` wrong JS default | trivial, masked | **FIXED** (forced by F5's fix) |
| F5 | unset `maxRetransmits`/`maxPacketLifeTime`: -1 native vs 0 web — and a *set* `maxRetransmits: 0` was unreadable on web | medium-high | **FIXED** |
| F6 | `webrtc-native` aborts the editor (exit 134) on first project scan | CI nuisance | open (upstream) |
| F7 | pre-open `get_id()`: 65535 native vs 0 web | low | **FIXED** (same root cause as F5) |
| F8 | `maxPacketLifetime` typo: both unreliable transfer modes were fully reliable **on every platform** | **high** | **FIXED** |

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

**F3** was the only finding with real design content, and parity settled it:
native never drops, so `_on_message` now grows the receive `RingBuffer` instead
of discarding the packet, `network/limits/webrtc/max_channel_in_buffer_kb`
becomes the buffer's **initial** size rather than a ceiling (it only ever
affected web — the extension keeps its own queue), the buffer shrinks back once
the queue empties, and growing past 8 MiB warns once so a channel nobody polls
is still visible. `packet_buffer` became a `Vector<uint8_t>` sized on demand,
which is what lets a single 256 KB packet through. After the fix `burst`
delivers 500/500, `sizes` delivers every payload up to 256 KB, and a scratch
12 MB-in-one-tick run delivered all 1200 packets in order with one warning.

**`get_buffered_amount()` was the last thing that looked like a divergence, and
it is not one.** It was recorded as "web reports the payload, native reports 0".
`buffered` settles it: four identical native runs report 0, 720000, 1580000 and
3430000 bytes on the send tick, so native's figure is nondeterministic and the
original row was one sample of it. libdatachannel's `send()` writes straight
into usrsctp and only queues when that would block, which an in-process peer
never forces; the browser queues everything and drains it on the event loop the
script is monopolising. Both report their own end's queue accurately, both
settle to 0 exactly when delivery completes, and nothing needed fixing. The
genuine finding underneath is smaller: the two ends' counters have no
happens-before relation (web's receiver can see ~61 KB before the sender's
counter drops), so **`get_buffered_amount()` says nothing about what the peer
holds**. `webrtc_web.md` §4.3 has the full experiment.

Not yet covered: `MultiplayerAPI`/`@rpc`, `dlink_enabled=yes`, and
Firefox/Safari.

## Deferred: this tier never applies bandwidth pressure

**Read this before concluding anything about flow control or unreliable
delivery.** Every fixture here runs over an **in-process loopback** — two peers
in one process (native) or one page (web), over localhost, unthrottled,
lossless, sub-millisecond RTT. That is what makes `compare.sh` possible, and it
leaves a class of behavior entirely unmeasured:

- **Native's `get_buffered_amount()` under sustained congestion.** It *can*
  report a backlog (0 → 3.4 MB run to run at 10 MB, 1.06 MB at 200 MB), but
  whether it *tracks* one usefully is untested — and it is the only
  flow-control signal the API has.
- **Web's send-queue ceiling.** Chrome caps a channel's send buffer
  (historically 16 MiB); native's `mSendQueue` does not. Only a congested link
  reaches it. Most likely place a new divergence is still hiding.
- **The F3 fix at the receiving end under congestion** — the receive buffer
  grows without a ceiling, and a fast loopback never tests what "without a
  ceiling" costs.
- **Anything timing-shaped**: retransmission, `maxPacketLifeTime` /
  `maxRetransmits` actually taking effect (their values are compared; their
  behavior never is — an unreliable channel is indistinguishable from a
  reliable one on a lossless link), ICE restarts, a link that dies rather than
  closes.

No fixture changes are needed — `buffered.gd`, `burst.gd` and `grow.gd` already
sample the right things. Only the environment is missing:

```bash
# Cheapest: netem on loopback. Native only, needs root, and it throttles ALL
# localhost traffic on the machine while applied — run nothing else.
sudo tc qdisc add dev lo root netem rate 1mbit delay 50ms
../../bin/godot.linuxbsd.editor.x86_64 --headless --path fixtures/loopback_project -- buffered
sudo tc qdisc del dev lo root
```

Two traps: `buffered.gd`'s 1200-tick timeouts are sized for a fast link and
will need raising, and a shaped `lo` may not touch the path at all for two web
peers inside one page — **check the delivery counts to confirm the shaping
applied** instead of assuming. Covering web properly needs a real off-box peer,
which needs the signaling server this tier deliberately skipped.

Rationale, the full four-item list and what each possible outcome would change:
`webgpu_notes/webrtc_web.md` §4.4, `TASKS.md` Task 18.12 (`DEFERRED`).
