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

There are **two entry points**, because the diff cannot run everywhere:

- **`compare.sh`** is the real parity check, and needs the `webrtc-native`
  reference extension — a separate 581 MB clone whose built libraries are
  gitignored. Use it whenever you have it.
- **`run_tests.mjs`** is the CI entry point and the **web half only**. Instead
  of diffing, it asserts what each fixture is supposed to *prove*, named in
  prose, so a failure says which behavior regressed. Every F1-F5/F7/F8 fix has
  a fixture line that changes if it breaks, so this catches a regression on a
  fresh checkout where `compare.sh` cannot run at all.

A green `run_tests.mjs` therefore means "no fix regressed"; a green
`compare.sh` means "web and native agree".

## Running

Build the native editor and the web templates first (see CLAUDE.md). Then:

```bash
# Web half only — the CI entry point. Needs no reference extension.
./export.sh            # all three variants, skipping any whose template is absent
node run_tests.mjs     # every fixture x every export present, in Chromium

RTC_BROWSERS=chromium,firefox node run_tests.mjs
RTC_VARIANTS=dlink RTC_FILTER=rpc node run_tests.mjs
```

```bash
# Native-vs-web parity. Needs the reference peer, built once
# (see webgpu_notes/webrtc_web.md §3):
cd webrtc-native
scons platform=linux target=template_debug arch=x86_64 build_profile=build_profile.json -j$(nproc)
cp -r bin/extension/addons/webrtc_native ../fixtures/loopback_project/addons/
cd ..

./compare.sh rpc                                  # nothreads, Chromium
RTC_DIR=exports/loopback_dlink ./compare.sh mesh   # another variant
RTC_BROWSER=firefox ./compare.sh loopback          # another browser

# One run, no comparison:
RTC_DIR=exports/loopback_threads RTC_TEST=mesh node run_loopback.mjs
```

`compare.sh` takes `RTC_DIR` (which export; default `exports/loopback`),
`RTC_BROWSER` and `EDITOR_BIN`. `export.sh` takes `EDITOR_BIN`, `RTC_THREADS=no`,
`RTC_DLINK=no` and `RTC_TEMPLATE_ZIP` (a single template override, for CI, which
builds only the dlink one). The runner serves the export itself with COOP/COEP,
which threaded exports require, and prints `crossOriginIsolated` so a silent
failure is visible.

The first editor run against the project exits 134 with the reference extension
present (F6, upstream) — `export.sh` does that scan separately and ignores it.

### Variants and browsers

| export | preset | notes |
|---|---|---|
| `exports/loopback` | `WebGPU` | nothreads, no GDExtension support — the default |
| `exports/loopback_threads` | `WebGPU-Threads` | `thread_support=true`; needs COOP/COEP, which the runner sends |
| `exports/loopback_dlink` | `WebGPU-Dlink` | `extensions_support=true` on the nothreads dlink template; needs the `misc/emsdk_patches/` toolchain patches |

`RTC_BROWSER`/`RTC_BROWSERS` is `chromium` (default), `firefox`, or `safari`.
Safari is macOS-only and driven by AppleScript rather than Playwright, exactly
as `scene_smoketest/run_scenes.mjs` drives it, and needs Safari's
Develop → "Allow JavaScript from Apple Events". **It has never actually been
run** — see "Current result".

A fixture's output is read from `window.__rtc_lines`, which
`tests/rtc_test_base.gd` appends to alongside every `print()`. The console is
used only for the error channel: polling the array works in every browser
including Safari, where the console cannot be read at all, and it prints each
line exactly once — which the diff depends on.

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
| `rpc` | `MultiplayerAPI`/`@rpc` — the layer projects write against: two `SceneMultiplayer` instances over two `WebRTCMultiplayerPeer`s, `@rpc` both directions, `get_remote_sender_id()`, a target-less broadcast, all three transfer modes |

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

**All nine fixtures compare identical to native across three export variants
and two browsers — 9 x 3 x 2 = 54 clean diffs — and 18 of 18 channel observables match**
(15 on the first pass). Every confirmed divergence is fixed: F1-F5, F7 and F8.
F6 is an upstream webrtc-native nuisance affecting this harness rather than the
engine. Two further findings, **F9 and F10, came from the sibling congestion
tier** (`webgpu_tests/webrtc_congestion/`) and could not have been found here:
both need a link that actually drops packets. F10 was the more serious — web's
unreliable transfer modes losing 75-87% of their packets — and is fixed.

**Safari is the one browser never run.** The driver exists and `local_ci.sh`
invokes it on macOS, but this work was done on Linux, and Playwright's `webkit`
is not Safari and has no WebGPU, so it is not a stand-in. Treat a first Safari
failure as a new data point, not a regression.

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

**Firefox was the uncertain one, and it is at parity.** Its WebRTC stack
diverges from Chrome's considerably more than its WebGPU one does, and nothing
in `modules/webrtc/` had ever run on it. All nine fixtures read identically to
native. Only two observables differ between the browsers at all, and `norm()`
already collapses both: SDP length (`loopback`: Chrome 457/456, Firefox 492/491,
native 444/444) and ticks to connect (Chrome 3, Firefox 13 — Firefox is slower
to gather candidates).

The only thing left uncovered is deliberate, and it is the next section.

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
