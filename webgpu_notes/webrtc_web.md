# WebRTC on the Web Target — Feasibility Evaluation and Hardening Plan

**Status: every confirmed divergence is fixed, and `compare.sh` is identical on
all eight fixtures, on both web variants.** F1, F2, F3, F4, F5, F7 and F8 are
fixed and measured; F6 is an upstream webrtc-native nuisance that affects only
the harness. **18 of 18 channel observables now match**, where the first pass
had 15. F8 is worth singling out: a cross-platform correctness bug (both
unreliable transfer modes were reliable *everywhere*) that this tier
structurally could not find, since both platforms were equally wrong. The
`get_buffered_amount()` suspicion is settled as **not a divergence** (§4.3).

Remaining: `MultiplayerAPI`/`@rpc` coverage, `dlink_enabled=yes`, and the
Firefox/Safari matrix — all coverage, no known defects. **Deliberately deferred:
everything that needs real bandwidth pressure (§4.4) — this tier runs entirely
over an unthrottled in-process loopback, so an unreliable channel cannot be
told from a reliable one here.**
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

### F1 — `get_max_packet_size()`: 1200 (web) vs 16384 (native) — CONFIRMED, FIXED

- Web: `return 1200;` (hardcoded, `webrtc_data_channel_js.cpp:143`).
- Native: `return 16384;` with an explicit citation of RFC-8831 §6.6
  (`WebRTCLibDataChannel.cpp:223`).

A 13.6x divergence. **Severity revised down to low-medium** after a second
finding: `get_max_packet_size` is **not bound to GDScript** — it is a
`PacketPeer` C++ virtual, and `WebRTCDataChannel::_bind_methods()`
(`modules/webrtc/webrtc_data_channel.cpp:36-51`) does not expose it. So this
only affects C++ consumers of `PacketPeer::get_max_packet_size()`, not user
scripts, and the harness **cannot assert it from GDScript** — it needs either a
C++-side check or a behavioral test (send 1201..16384 bytes and observe).

Separately, the web `put_packet()` enforced no limit at all
(`webrtc_data_channel_js.cpp:135-141`), passing any size to `ref.send()`, so an
oversized send failed inside JS with no `Error` returned. Native validates
(`!channel`, `channel->isClosed()`) and catches `std::exception` to return
`FAILED` with the message printed.

**Behavioral test (`tests/sizes.gd`): the 1200 was a wrong number, not an
enforced limit.** A sweep of 1200 / 1201 / 4096 / 16384 / 16385 / 65530 / 65531
/ 262144 bytes, recording `put_packet()`'s error, the channel state right after
the send, and whether the peer got the payload intact:

| payload | native | web |
|---|---|---|
| 1200 .. 65530 | `put_err=0`, DELIVERED, intact | `put_err=0`, DELIVERED, intact |
| 65531 | `put_err=0`, DELIVERED | `put_err=0`, **DROPPED** (`Buffer full!`) |
| 262144 | `put_err=0`, DELIVERED | `put_err=0`, **DROPPED** (`Buffer full!`) |

Chrome accepts and delivers 256 KB sends with no error and never kills the
channel, so nothing on the send path ever enforced 1200 — it only reported it.
**The two divergences left in that table are F3, not F1**: 65530 bytes is the
exact ceiling of the default 64 KB receive `RingBuffer`
(`space_left()` caps at `size - 1` and `_on_message` needs `p_size + 5`), so
one byte more is a receive-side drop. Raising it belongs to F3.

**Fix applied** (both halves, upstreamable):

- `get_max_packet_size()` returns **16384**, native's value, with the RFC-8831
  §6.6 citation. Note `WebRTCMultiplayerPeer::get_max_packet_size()` still
  hardcodes `1200` — but that is the *shared* file, so it understates equally on
  every platform and is not a parity divergence.
- `godot_js_rtc_datachannel_send()` wraps `ref.send()` in try/catch, logs the
  size and the exception through `GodotRuntime.error`, and returns 1;
  `put_packet()` turns that into `FAILED`. Previously the exception escaped into
  the engine's call stack, where nothing could handle it.

### F2 — Buffered packets destroyed on remote close (web only) — CONFIRMED, FIXED

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
disconnect were silently lost; on desktop they are delivered. Note
`avail_after_close=0` on web — the count was zeroed too, so **both** mechanisms
fired: the `STATE_OPEN` guard *and* the buffer wipe.

**Fix applied**, both mechanisms, in `webrtc_data_channel_js.cpp`:

- `get_packet()` drops the `STATE_OPEN` guard entirely and fails only on an
  empty queue, which is exactly what native's `_get_packet()` does.
- `close()` no longer touches `in_buffer`, `queue_count` or `_was_string`; it
  only closes the channel, as native's `_close()` does. The buffer is released
  with the object. The feared `_on_close`/`_on_error` ordering problem did not
  materialise: both still call `close()`, and since `close()` no longer
  destroys state there is no ordering to get wrong. The cost is that a closed
  channel holds its receive buffer (≤ 64 KB) until it is freed — the same
  trade native makes with its queue.

Measured after the fix, identical on both platforms:

```
native:  f2 ch2_closed_after=2 ticks avail_after_close=1
         f2 drain_after_close len=10 content='f2-pending'   -> verdict=DRAINED
web:     f2 ch2_closed_after=1 ticks avail_after_close=1
         f2 drain_after_close len=10 content='f2-pending'   -> verdict=DRAINED
```

**Also now tested at the multiplayer layer** (`tests/mesh_close.gd`, the gap
Task 18.5 left open): three packets queued on a two-peer mesh and deliberately
never drained, then `mp1.close()`. The fixture reads twice, because the layer
has two places a packet can die:

| read | native | web (after the fix) |
|---|---|---|
| through `WebRTCMultiplayerPeer` | `avail=0` → **LOST** | `avail=0` → **LOST** |
| through a channel `Ref` held since connect | `avail=3`, 13 bytes → **DRAINED** | `avail=3`, 13 bytes → **DRAINED** |

So `./compare.sh mesh_close` is **identical** on both platforms. The first row
is a **structural, platform-independent loss and not F2**:
`WebRTCMultiplayerPeer::poll()` calls `remove_peer()` the moment a channel
leaves `STATE_OPEN`, which drops the peer's channels outright, so the packets
become unreachable through that API on desktop exactly as on web. Worth
documenting as its own limitation — a project that wants the final packets of a
disconnecting peer cannot get them through `WebRTCMultiplayerPeer` on any
platform — but it is upstream's shared design, not a web divergence.

### F3 — Silent drop vs unbounded queue on backpressure — CONFIRMED, FIXED

- Web: fixed `RingBuffer` sized from
  `network/limits/webrtc/max_channel_in_buffer_kb` (default 64 KB), and
  `_on_message` drops the packet when it is full
  (`ERR_FAIL_COND_MSG(in_buffer.space_left() < (int)(p_size + 5), "Buffer full! Dropping data.")`, `:78`).
- Native: an unbounded `std::queue` (`shared_state->packet_queue`), which never
  drops.

So a reliable-ordered channel — contractually lossless — **can lose data on web
but not on desktop** if the application polls slowly. The two implementations
have made opposite trade-offs: web bounds memory and sacrifices the delivery
guarantee; native honors the guarantee and lets memory grow. Neither is
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

#### The fix: grow the buffer instead of dropping

The choice was between the two trade-offs above, and parity decides it: native
never drops, so web must not either. `_on_message` now enlarges `in_buffer`
when a packet does not fit, rather than discarding the packet.

- **`_grow_in_buffer()`** picks the next power of two that holds the queued
  bytes plus the new packet plus the one byte `RingBuffer::space_left()`
  reserves, and calls `RingBuffer::resize()`, which preserves queued data when
  growing (including the wrapped `read_pos > write_pos` case — see
  `tests/grow.gd`). The configured
  `network/limits/webrtc/max_channel_in_buffer_kb` becomes the **initial** size
  rather than a ceiling; `doc/classes/ProjectSettings.xml` now says so, and
  notes that the setting only ever affected the web implementation anyway (the
  extension keeps its own queue and ignores it).
- **`_shrink_in_buffer()`** returns the buffer to that configured size once the
  queue empties, so one burst does not hold its peak allocation for the
  channel's lifetime. It only runs on an empty buffer, because
  `RingBuffer::resize()` cannot preserve data while shrinking.
- **A runaway queue is still visible**, which was the other half of the
  complaint: growing past **8 MiB** prints one warning per channel naming the
  packet count, the polling fix and the project setting. The hard stop is
  1 GiB, which keeps the shift arithmetic sane and is unreachable in practice.
- **The per-packet buffer is dynamic too.** `packet_buffer` was a fixed
  `uint8_t[65531]` member, so even with a bigger queue a single packet above
  65530 bytes could not have been handed back. It is a `Vector<uint8_t>` sized
  on demand, which is what makes the 256 KB row of `sizes` deliverable.
- **Packets can still be dropped in exactly one place**: an allocation failure
  or the 1 GiB stop, where the original `Buffer full! Dropping data.` error
  remains.

Measured after the fix, both web variants, against unchanged native:

| | native | web before | web after |
|---|---|---|---|
| `burst`: 500 x 1000 B in one tick | 500 / 500 | **65 / 500** | **500 / 500** |
| `sizes`: 65531 B single packet | delivered | **dropped** | **delivered** |
| `sizes`: 262144 B single packet | delivered | **dropped** | **delivered** |
| `grow`: 2100 packets over 3 grow/partial-drain/regrow rounds | lossless, in order | — | lossless, in order |
| 12 MB in one tick (1200 x 10 kB, scratch run) | — | — | all 1200 delivered in order, one 8 MiB warning |

`./compare.sh burst` and `./compare.sh grow` now differ from native on nothing
but `get_buffered_amount()` (§4.3) and tick counts, and `grow` is **identical**.
`threads=yes` matches `nothreads` on all six fixtures.

### F4 — `is_negotiated` wrong default (web) — CONFIRMED COSMETIC, FIXED

`library_godot_webrtc.js:170-174` passes `65535` as the fallback for a boolean
getter (copy-pasted from `id_get` directly above), where the neighboring
`is_ordered` correctly passes `true`. Native stores and returns a real bool
(`WebRTCLibDataChannel.cpp:164-167`).

Every current browser exposes `negotiated` as a real boolean, so the fallback
never fires. Latent, not live. One-line fix.

**Fixed as a consequence of F5, and it had to be.** F5's fix makes `get_prop`
fall back on `null` as well as `undefined`, which turns every default in that
file from dead code into live code — including this one. Leaving `65535` there
would have promoted a latent bug into a reachable one that reads an absent
value as `negotiated = true`. It is now `false`. The same audit covered the
remaining defaults: `ordered` → `true` (correct, WebRTC orders by default),
`id` → `65535` (correct, and what fixes F7), `bufferedAmount` → `0` (correct).

### F5 — Unset reliability values: -1 (native) vs 0 (web) — CONFIRMED, FIXED

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
cannot be trusted as documentation of behavior.

Both values *are* script-exposed (`get_max_retransmits()`,
`get_max_packet_life_time()`), so unlike F1 this is directly visible to user
code: GDScript branching on these sees `0` on web and `-1` on desktop. Cheapest
real fix in the list, and the most likely to be silently relied upon.

**Severity raised again, by `tests/chancfg.gd` (see F8):** a channel created
with `maxRetransmits: 0` reads back `rtx=0` on web — the identical value web
reports for *unset*. So this is not only about how a default prints: on web a
deliberately-set `maxRetransmits: 0` cannot be told apart from no setting at
all, and any code that round-trips or inspects channel reliability is reading a
value it cannot interpret.

**Severity raised after the multiplayer-layer test (§4):** F5 is the *only*
divergence that reaches `WebRTCMultiplayerPeer`, and it shows on all three
channels it auto-creates for every peer — so it is not a corner case of
hand-rolled channels, it is present in every project that uses the multiplayer
peer.

#### The fix: `null` is absence, not zero

One change at the root, in `get_prop`:

```js
get_prop: function (p_id, p_prop, p_def) {
    const ref = IDHandler.get(p_id);
    if (!ref || ref[p_prop] === undefined || ref[p_prop] === null) {
        return p_def;
    }
    return ref[p_prop];
},
```

plus the two reliability defaults corrected from the unreachable `65535` to
**-1**, which is what the other implementations report for unset. The
`maxPacketLifeTime` getter, which had its own hand-rolled `undefined` checks and
a `65535` fallback, now goes through `get_prop` as well (keeping the legacy
`maxRetransmitTime` fallback).

**It fixes F7 and forces F4.** `id`'s `65535` default was dead for exactly the
same reason — browsers report `id` as `null` before a channel opens — so making
`null` absent revives it, and `65535` happens to be precisely what native
reports pre-open. F7 closes with no further change. F4's `65535` default for a
*boolean* also becomes reachable, so correcting it to `false` is part of this
fix rather than an optional extra; see F4.

Measured after the fix, `tests/chancfg.gd`, **identical on native and both web
variants**:

| channel config | lifetime | rtx |
|---|---|---|
| `{ordered}` baseline | **-1** | **-1** |
| `{ordered, maxPacketLifeTime: 1}` | 1 | -1 |
| `{!ordered, maxRetransmits: 0}` | -1 | **0** |

The last row is the point: `maxRetransmits: 0` and "unset" are finally
different values on web. And `loopback`'s pre-open line is now byte-identical
across platforms, `id=65535 lifetime=-1 rtx=-1` on both.

**Behavior change for projects, on web only**: `get_max_retransmits()` and
`get_max_packet_life_time()` now return `-1` instead of `0` for an unconfigured
channel, and `get_id()` returns `65535` instead of `0` before a channel opens.
Script that compared against `0` will need `-1` — which is the point, since
desktop always returned `-1` and such code was already platform-dependent.

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

### F7 — Pre-open `get_id()`: 65535 (native) vs 0 (web) — CONFIRMED, FIXED

Measured (§4): before a channel opens, native reports `id=65535` and web reports
`id=0`. Same `null`-vs-`undefined` mechanism as F5. **Post-open both report
`id=1`**, so this affects only code that reads `get_id()` before the channel is
open. Low severity, but it invalidates the note in the §3 baseline table that
claimed pre-open `id` agreed across platforms — that comparison was against the
JS *default*, not the measured value.

**Fixed by F5's root-cause fix, with no change of its own.** The `65535`
default in `id_get` was unreachable because browsers report `id` as `null`
pre-open; once `get_prop` treats `null` as absence, the default fires — and it
already matched native's sentinel. Measured: `pre_open ... id=65535` on native
and web alike, with `get_id()` still returning the real `1` once open.

### F8 — `maxPacketLifetime` typo: both unreliable transfer modes were fully reliable, on every platform — CONFIRMED, FIXED

**Not a parity divergence. A correctness bug that both platforms shared, which
is exactly why this tier could not see it.** Found by a separate code review,
not by `compare.sh`: the two sides agreed, so the diff was clean.

`WebRTCMultiplayerPeer` configures its three per-peer channels with
`cfg["maxPacketLifetime"]` — lowercase `t`
(`webrtc_multiplayer_peer.cpp:223,226,314`). Everything that reads it spells it
`maxPacketLifeTime`:

- web: the config `Dictionary` is passed straight to `createDataChannel()` as
  JSON, and `RTCDataChannelInit` defines `maxPacketLifeTime`; an unknown member
  is ignored.
- native: `WebRTCLibPeerConnection.cpp:119-121` checks
  `p_dict.has("maxPacketLifeTime")`.
- the getter both back ends expose is `get_max_packet_life_time()`.

So the key was silently dropped and **`TRANSFER_MODE_UNRELIABLE_ORDERED` and
`TRANSFER_MODE_UNRELIABLE` behaved as reliable-ordered and reliable-unordered**
on desktop, mobile and web alike. Upstream since `ca7d572908` (2022-07-12,
"[Net] Modularize multiplayer"), present in `upstream/master` today, and not
fork damage.

**The evidence was already in this document and went unread.** §4's mesh table
shows all six channels reporting `lifetime` *unset* — `-1` native, `0` web — on
channels labeled `ordered` and `unreliable` whose config asks for
`maxPacketLifetime = 1`. That was recorded as F5 (how *unset* prints) without
asking why the value was unset at all.

**Measured directly** by `tests/chancfg.gd`, which creates four channels, one
config each, and reads every accessor back on both ends. Identical verdict on
both platforms:

| channel config | native | web |
|---|---|---|
| `{ordered}` baseline | lifetime unset | lifetime unset |
| `{ordered, maxPacketLifetime: 1}` (the typo) | **lifetime unset** | **lifetime unset** |
| `{ordered, maxPacketLifeTime: 1}` (correct) | **lifetime = 1** | **lifetime = 1** |
| `{!ordered, maxRetransmits: 0}` | rtx = 0 | rtx = 0 |

`chancfg verdict=TYPO_DROPPED` on both. **Fix**: one character, three lines.
After it, `mesh` reports `lifetime=1` on channels 2 and 3 on both platforms
(was unset), all three transfer modes still deliver over loopback, and no other
fixture changes.

**What the fix changes for projects**, stated plainly because it is a behavior
change on *every* platform, not just web: channels 2 and 3 now carry a 1 ms
packet lifetime, so SCTP stops retransmitting them almost immediately — which
is what "unreliable" is supposed to mean, and what `MultiplayerPeer`'s documented
contract promises. A project that was unknowingly getting reliable delivery on
an unreliable channel will start seeing loss under congestion. That is the
intended semantics, and `1` is upstream's own chosen value
(`add_peer()`'s `p_unreliable_lifetime` default).

**Untestable here, and it matters**: on a lossless in-process loopback nothing
needs retransmitting, so an unreliable channel is **indistinguishable** from a
reliable one — the fix's actual effect cannot be observed by this tier at all,
only its configuration. That is §4.4's gap, and F8 is now the most concrete
reason to close it.

**Side finding, from the same fixture: F5 is worse than cosmetic.** The `rtx0`
channel sets `maxRetransmits: 0` deliberately, and web reports `rtx=0` — the
same value it reports for *unset*. So on web a genuinely-set
`maxRetransmits: 0` is **indistinguishable from no setting at all**, which is
not a cosmetic difference in how a default prints. F5's severity should be read
up accordingly.

---

### F9 — Web's data-channel send queue caps at ~16 MiB; native's does not — CONFIRMED (new, 2026-10-13)

**Severity: low-medium.** Chrome refuses a send once a channel's buffered
amount reaches **16,770,584 bytes**; libdatachannel accepted 24 MB with no cap.
Predicted in §4.4 as the likeliest remaining divergence, and it was there.

It is graceful on every axis that matters: the channel stays `STATE_OPEN`,
`put_packet()` returns `FAILED` (only because the F1 fix stopped the JS
exception being swallowed — before F1 this was silent loss with no API signal),
and a retry after the queue drains below 2 MB is accepted, with everything
previously accepted arriving intact. An application that treats `FAILED` as
"retry later" is correct on both platforms.

Measured by `webgpu_tests/webrtc_congestion/` `send_ceiling.gd`; numbers and
method in §4.4a.2. **Documentation, not a code change.**

### F10 — On web, the unreliable transfer modes lose most of their packets — CONFIRMED (new, 2026-10-13)

**Severity: high for any project using `TRANSFER_MODE_UNRELIABLE` or
`TRANSFER_MODE_UNRELIABLE_ORDERED` on web.** At 5% link loss and 25 ms one-way
delay, the exact channel configuration `WebRTCMultiplayerPeer::add_peer` builds
(`maxPacketLifeTime` = its default of **1** ms) delivers **13–25%** of its
packets on web against **~95%** on native. A channel with
`maxPacketLifeTime: 50` agrees across platforms at ~95%, so the divergence is in
the lifetime *value*, not the mechanism.

Three things keep this in proportion, all measured rather than argued:

- A 1 ms lifetime causes **no** loss by itself — on the same link with 0% loss
  both platforms deliver 100%. The lifetime governs retransmission only.
- It is therefore **invisible on a loopback or a clean LAN**, which is every
  environment this project tested in before. A developer cannot reproduce it
  locally; a player on wifi or mobile will hit it.
- It is a **consequence of fixing F8, not a regression from it**: before that
  fix the lifetime was never applied, so both modes were silently fully
  reliable everywhere.

Unlike F9 there is **no API signal** — `put_packet()` returns `OK` for every
packet that is then never delivered. Measured by
`webgpu_tests/webrtc_congestion/` `loss_modes.gd`; numbers in §4.4a.4.
**No code change made**: the behavior follows the configuration, and
`unreliable_lifetime = 1` is an upstream default, so the upstream conversation
comes first. Fork-local mitigation is to pass a larger `unreliable_lifetime`
to `add_peer()` on web.

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
signaled to Playwright by setting `window.__rtc_done` through
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
| `get_id()` pre-open | 65535 | 65535 (was 0) | match since the F7 fix |
| `get_id()` post-open | 1 | 1 | match |
| `get_max_packet_life_time()` unset | -1 | -1 (was 0) | match since the F5 fix |
| `get_max_retransmits()` unset | -1 | -1 (was 0) | match since the F5 fix |
| `get_protocol()` | `''` | `''` | match |
| `is_negotiated()` | false | false | match |
| `get_buffered_amount()` | 0 | 0 | match |
| `get_write_mode()` default | 1 (BINARY) | 1 (BINARY) | match |
| binary round-trip + `was_string_packet()` | ok, false | ok, false | match |
| text round-trip + `was_string_packet()` | ok, true | ok, true | match |
| drain after remote close | **DRAINED** | **DRAINED** (was LOST) | match since the F2 fix |
| payload 1200 .. 65530 bytes | delivered intact | delivered intact | match |
| payload 65531 / 262144 bytes | delivered intact | delivered intact (was dropped) | match since the F3 fix |
| 500 x 1000 B burst, drained late | 500 / 500 | 500 / 500 (was 65 / 500) | match since the F3 fix |
| `get_buffered_amount()` right after a send | 0 to 3.4 MB, varies per run | the payload size | not a divergence — §4.3 |

**All 18 of the original observables now match** — 15 did on the first pass, 17
after F2, and 18 after F5 and F7. The added rows come from `tests/sizes.gd`,
`tests/burst.gd`, `tests/grow.gd` and `tests/buffered.gd`; the
`get_buffered_amount()` row is **not** a divergence — both ends report their own
queue accurately and native's figure is nondeterministic (§4.3 has the
experiment that settled it) — and `tests/chancfg.gd` adds the channel-config
round-trips, also identical.

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
`__proxy: 'sync'` marshaling that every function in `library_godot_webrtc.js`
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

### §4.3 The `get_buffered_amount()` divergence — RESOLVED: not a divergence

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

**The F1 sweep (§2 F1) is a stronger version of the same reading and says the
same thing**: across all eight payload sizes, read immediately after each send,
web reports exactly the payload (`buffered=1200`, `...=16384`, `...=262144`)
and native reports `0` every time, including for 256 KB. That is now eight
independent samples rather than one incidental read, but it still does not
settle the question — in-process libdatachannel plausibly flushes synchronously
on `send()`. It stays open, and a test that keeps a known quantity in flight is
still the thing that would close it.

#### Settled by `tests/buffered.gd`: both values are correct, and there is
#### nothing to fix

The experiment: queue 10 MB in one tick, then sample the sender's
`get_buffered_amount()` **and** the receiver's packet count together every
tick, draining nothing until the end — so the receiver's count measures only
what has crossed the wire. Three things came out of it, and the first one
dissolves the finding.

**1. Native does not report `0`. It reports whatever is genuinely still
queued, which is nondeterministic.** Four consecutive native runs of the same
fixture, same payload, read on the send tick:

```
buffered=0        receiver_has=989 of 1000
buffered=720000   receiver_has=907
buffered=1580000  receiver_has=821
buffered=3430000  receiver_has=636
```

A scratch native run at 20x the payload (200 MB) reported `buffered=1060000`
with 19872 of 20000 already delivered. So the original "native says 0" was a
**single sample of a quantity that varies from 0 to 3.4 MB between runs** — the
one reading that started this whole question happened to catch the fast end.
Reading it once and treating it as the platform's answer was the mistake, not
the implementation.

**2. The mechanism, confirmed in libdatachannel's source.**
`SctpTransport::send()` (`src/impl/sctptransport.cpp:374-394`) flushes its queue
and tries to write the message straight into usrsctp; it only pushes onto
`mSendQueue` and calls `updateBufferedAmount()` **when that write would block**.
An in-process peer drains as fast as the sender produces, so native's queue is
usually near-empty and `bufferedAmount` is near-zero. The browser, by contrast,
queues everything and drains it on the event loop — which the GDScript send
loop is monopolising — so it necessarily reports the whole 10 MB. Both numbers
are an accurate account of each end's own queue; what differs is how fast that
queue drains, and the in-process loopback is exactly the configuration that
flatters native. Confirmation from the other direction: the **threaded** web
export drains the same 10 MB in **1 tick** instead of 17, because its
networking is not gated behind the starved main-thread loop.

**3. A real finding fell out of it: the two ends' counters are not
comparable mid-flight.** The fixture first asserted "the sender never claims to
owe more than the receiver is missing", which web violates by up to ~61 KB: in
a one-page loopback the receiver can see messages before the sender's own
`bufferedAmount` has been decremented. There is no happens-before relation
between the two counters, so **`get_buffered_amount()` is only meaningful about
the end you read it from** — a project cannot use it to infer what the peer has.
That is now what the fixture records (`ends_disagree_by_at_most`) instead of
asserting.

**What the fixture guards**, identical on both platforms and both web variants:
every sample is in range (`0 ≤ buffered ≤ queued`), everything sent arrives in
order, and the sender settles at `0` exactly when delivery completes.
`./compare.sh buffered` is **identical**; the per-platform numbers live on lines
the fixture marks `curve`, which `norm()` collapses (the raw output keeps them)
precisely because they are legitimately run- and platform-dependent.

**Not covered**: native's `bufferedAmount` against a congested *remote* link.
The code path that reports a backlog is proven to exist and the 200 MB run
exercises it, but no test here applies real bandwidth pressure. That gap is
deliberate and written up as §4.4 rather than left implicit.

### §4.4 Bandwidth pressure — was deferred, now MEASURED (2026-10-13)

**Status: DONE. All four unknowns below are answered, and two of them produced
new findings (F9 and F10).** Results are in §4.4a; the tier is
`webgpu_tests/webrtc_congestion/`. The original deferral text is kept intact
below because the four questions it framed are what got measured, and because
the plan it sketched turned out to be wrong in one important way — see §4.4a's
note on shaping without root.

Every result in this document comes from an **in-process loopback**: two peers
in one process (native) or one page (web), over localhost, with no bandwidth
limit, no loss and sub-millisecond RTT. That is the right default — it made the
parity question answerable in a day, and it is why `compare.sh` works at all —
but it means **one whole class of behavior is unmeasured**: what either
implementation does when the link cannot keep up with the application.

#### What is actually unknown

1. **Native's `get_buffered_amount()` under sustained congestion.** §4.3 proved
   libdatachannel *can* report a backlog (0 → 3.4 MB run to run at 10 MB, and
   1.06 MB at 200 MB), and the code path is explicit
   (`SctpTransport::send()` → `mSendQueue.push()` → `updateBufferedAmount()`,
   `src/impl/sctptransport.cpp:374-394`). What is unknown is whether the number
   *tracks a real backlog usefully* — rises with offered load, falls as the link
   drains — or only ever flickers. `get_buffered_amount()` is the **only**
   flow-control signal in Godot's WebRTC API, so a project that throttles on it
   is relying on exactly this untested property.
2. **Web's send-queue ceiling.** Chrome is documented to cap a data channel's
   send buffer (historically 16 MiB) and to fail or close the channel past it.
   A congested link is the only way to reach that, and since the F1 fix an
   oversized send now returns `FAILED` instead of vanishing, so the failure
   would be observable. Native's `mSendQueue` has no equivalent cap — this is a
   plausible **real divergence that no fixture here can reach**.
3. **The F3 fix under congestion, at the receiving end.** The receive buffer now
   grows without a ceiling. On a fast loopback it peaks at the burst size and
   shrinks straight back. Under congestion with a slow poller the interesting
   question is different: how large it gets, whether the 8 MiB warning fires at
   a useful point, and whether shrink-on-empty ever gets a chance to run.
4. **Everything timing-shaped that loopback hides**: ICE restarts, SCTP
   retransmission on real loss, `maxPacketLifeTime` / `maxRetransmits` actually
   taking effect (F5's values are compared, but their *behavior* never is —
   unreliable modes cannot be distinguished from reliable ones on a lossless
   link), and `peer_disconnected` on a link that dies rather than closes.

#### How to test it when the time comes

The fixtures need no changes — `buffered.gd`, `burst.gd` and `grow.gd` already
sample what matters. Only the environment is missing. In increasing order of
cost:

- **`netem` on loopback (cheapest, needs root).** Rate-limit and delay `lo`
  for the duration of a native run:

  ```bash
  sudo tc qdisc add dev lo root netem rate 1mbit delay 50ms
  ../../bin/godot.linuxbsd.editor.x86_64 --headless --path fixtures/loopback_project -- buffered
  sudo tc qdisc del dev lo root
  ```

  Caveats to respect: this throttles **all** localhost traffic on the machine
  while it is in place, so nothing else should be running; the `buffered.gd`
  timeouts (1200 ticks) are sized for a fast link and will need raising; and it
  only covers native, since the browser's peers talk to each other inside the
  page and may never touch `lo` at all. Verify with the delivery counts, not by
  assumption — if the run finishes as fast as before, the shaping did not apply
  to the path being measured.
- **Two netns joined by a veth pair with `netem` on one side (middling).**
  Shapes exactly the path under test and leaves the rest of the machine alone.
  Still native-only for the same reason.
- **A genuine off-box peer (most faithful, most work).** This is the one that
  covers *web* under congestion, and it needs the thing this tier deliberately
  skipped: the Phase 1 signaling server, so two processes on two machines (or a
  container with shaped egress) can connect. Budget for that, not for the
  fixture.

#### What would change if it found something

- Native not tracking a real backlog → an upstream **libdatachannel /
  webrtc-native** report, and `get_buffered_amount()` documented as unusable
  for flow control on desktop.
- Web hitting a send-queue cap native does not have → a **new finding (F9)**,
  and the first one where web's limit is the browser's rather than Godot's.
- The receive buffer ballooning past anything reasonable → revisit F3's "grow
  without a ceiling" decision, which was taken on the strength of native doing
  the same thing and would deserve rethinking if the ceiling is what keeps a
  browser tab alive.

*(Two of these three predictions landed: the backlog does track, and the send
cap is real and is F9. The third was fine. The finding nobody predicted is F10.
See §4.4a.)*

### §4.4a Results (2026-10-13) — F9 and F10

Tier: `webgpu_tests/webrtc_congestion/` (procedure in its README). Four
fixtures, run native-vs-native and web-vs-native across a `netem`-shaped link,
each with an unshaped or lossless control. `./sweep.sh` reproduces the whole
table in ~15 minutes.

**The shaping plan in §4.4 was wrong in a way worth recording.** It proposed
`sudo tc qdisc add dev lo root netem ...`, which needs root and rate-limits all
loopback traffic on the machine until it is removed — so an interrupted run
leaves the host shaped. `shape.sh` instead runs under **`unshare -rnm`**, which
maps the user to root inside a new user + network + mount namespace and grants
`CAP_NET_ADMIN` *there only*. It builds two namespaces joined by a veth pair
with `netem` on both ends, needs **no root at all**, touches nothing outside the
test, and shapes exactly the path under test. The "middling" netns option §4.4
listed third is in fact the cheapest and the safest; the `lo` option should not
be used.

Because the peers are now two processes with real signaling, this is also the
first time in the project that **web and native WebRTC have talked to each
other** — every earlier result compared web-to-web against native-to-native.
A two-machine run (Chrome here, libdatachannel on another box, real LAN) is
validated too, as a control that none of this is a veth artifact.

#### 1. Native's `get_buffered_amount()` does track a real backlog

`backpressure.gd`, 3 MB offered at 500 KB/s over a 2 Mbit/s link with 25 ms
one-way delay:

| | peak buffered | goodput | delivered | verdict |
|---|---|---|---|---|
| native | 602,000 B | 220 kB/s | 3000/3000, 0 gaps | `TRACKED` |
| web | 929,672 B | 153 kB/s | 3000/3000, 0 gaps | `TRACKED` |
| **unshaped control** | **0** | 497 kB/s | 3000/3000 | `NO_PRESSURE` |

The counter rises under offered load and returns to 0 as the link drains, on
both platforms. **`get_buffered_amount()` is usable for flow control**, which
§4.3 could not establish — there the link never was the bottleneck, which is
exactly what the `NO_PRESSURE` control row reproduces on demand.

#### 2. F9 — web's send queue caps at ~16 MiB; native's does not

`send_ceiling.gd`, pushing with no pacing over an 8 Mbit/s link, probing to
24 MB:

| | outcome | peak queue |
|---|---|---|
| web (Chrome) | **refused at 16,770,584 B** after 17,104,000 B accepted | 16,439,556 B |
| native | no cap reached at 24,000,000 B accepted | 22,784,000 B |

So the cap §4.4 predicted is real and is almost exactly the documented 16 MiB.
Three things make it benign rather than dangerous, and all three are measured:

- **It is graceful.** The channel stayed `STATE_OPEN` (state=1); it does not
  close or reset.
- **It is visible.** `put_packet()` returns `FAILED` — only because of the F1
  fix, which stopped the JS `send()` exception being swallowed. Before F1 this
  would have been a silent data loss with no API signal at all.
- **It is recoverable.** After backing off until the queue fell below 2 MB, a
  retry was accepted, and all 1133 accepted packets arrived intact with 0 gaps.

**Severity: low-medium, documentation rather than code.** An application that
treats `FAILED` as "retry later" behaves correctly on both platforms; one that
treats it as fatal will differ. Native's figure is "no cap *below the probe
limit*", not "unbounded".

#### 3. F3's receive-buffer growth is sound under real pressure

`recv_growth.gd`, 12 MB pushed over an 8 Mbit/s link to a peer that polls the
channel but drains nothing for 25 s:

| | peak held | after draining | verdict |
|---|---|---|---|
| web | 3001 packets / 12,004,000 B | 3000 drained, 0 gaps, 0 left | `GREW_INTACT` |
| native | 3001 packets / 12,004,000 B | 3000 drained, 0 gaps, 0 left | `GREW_INTACT` |

The 8 MiB warning fires at exactly **8,388,608 bytes with 1047 packets queued**
and names both mitigations (poll more often, or raise
`network/limits/webrtc/max_channel_in_buffer_kb`) — so it fires at a useful
point, well before anything is at risk. Nothing was lost or reordered across a
growing, wrapping ring buffer, and shrink-on-empty ran: the post-drain count is
0 and agrees with the drain loop. **F3's decision stands.**

One correction to §4.4's own wording: the receive buffer does **not** grow
"without a ceiling". `webrtc_data_channel_js.cpp` refuses past
`IN_BUFFER_MAX_SHIFT` = **1 GiB** with an explicit "Dropping data" error. The
ceiling is simply far above anything these runs approach.

#### 4. F10 — on web, the unreliable transfer modes lose most of their packets

`loss_modes.gd` sends 200 paced packets down five differently-configured
channels at once, over a 4 Mbit/s link with 25 ms delay and **5% loss**. The
stream is paced at 80 KB/s so the loss comes from `netem`, not from a queue the
fixture overfilled, and the reliable channel is the control: if it loses
anything the run is discarded.

| channel | config | native | web (Chrome) |
|---|---|---|---|
| `reliable` | `{ordered:true}` | 100% | 100% |
| `rtx0` | `+ maxRetransmits:0` | 94–97% | 91–95% |
| `life50` | `+ maxPacketLifeTime:50` | 93–97% | 94–97% |
| **`life1`** | `+ maxPacketLifeTime:1`, ordered | **90.5–95.5%** | **28.5–42.5%** |
| **`life1u`** | `+ maxPacketLifeTime:1`, unordered | **92.5–99%** | **13–25.5%** |

Ranges are across 4 web and 3 native runs — sampled repeatedly on purpose,
since §4.3's lesson was that one sample of a nondeterministic quantity is how
this document got a finding wrong before.

**The two `life1*` rows are not hypothetical.** They are byte-for-byte what
`WebRTCMultiplayerPeer::add_peer` builds for `CH_ORDERED` and `CH_UNRELIABLE`
(`webrtc_multiplayer_peer.cpp:312-320`), whose `unreliable_lifetime` parameter
**defaults to 1** millisecond. So a project using `TRANSFER_MODE_UNRELIABLE`
over a lossy link gets ~13–25% delivery on web against ~95% on desktop.

What makes this readable:

- **A 1 ms lifetime does not cause loss by itself.** On the same link with
  **0%** loss, every channel delivers 100% on both platforms
  (`MODES_INDISTINGUISHABLE`) — the lifetime governs *retransmission*, not
  first transmission. It bites only once the link drops something, which is why
  no loopback and no clean LAN can see it. **A developer testing on either will
  never reproduce this; a player on wifi or mobile will.**
- **`life50` is the control and it agrees across platforms.** The divergence
  belongs to the lifetime *value*, not to the mechanism — so the mitigation is
  already proven by the same run: pass a larger `unreliable_lifetime` to
  `add_peer()`.
- **It is a consequence of fixing F8, not a regression from it.** Before that
  one-character fix the lifetime was never applied at all, so both unreliable
  modes were silently *fully reliable* on every platform. The fix made them
  honest; this measures what honest costs on each stack. Fixing F8 was still
  right — but F8's own note that its effect "is not verifiable on a lossless
  loopback" turned out to be hiding a much bigger number than anyone expected.

**Severity: high for any project that uses the unreliable modes on web.**
Unlike F9 there is no API signal: the packets are simply not delivered, and
`put_packet()` returns `OK` for every one of them. Next step is an upstream
question about whether `unreliable_lifetime = 1` is a sensible default at all,
with these numbers attached; the fork-local mitigation is to document it and
recommend a larger value on web. **No code change has been made for F10** — the
behavior is arguably correct per the configuration, and changing a default that
upstream shares deserves the upstream conversation first.

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

Deliverable: a green web-to-web tier. This alone characterizes F1-F4 as
*behaviors*, independently of whether desktop agrees.

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

- **Backpressure** (F3): **DONE and fixed** — `tests/burst.gd` quantified it
  (65/500) and `tests/grow.gd` guards the fix through three
  grow/partial-drain/regrow rounds; see §2 F3.
- **Close semantics** (F2): **DONE and fixed** — `tests/loopback.gd` at the
  channel layer and `tests/mesh_close.gd` at the multiplayer layer; see §2 F2.
- **Size limits** (F1): **DONE and fixed** — `tests/sizes.gd` sweeps 1200 /
  1201 / 4 KB / 16 KB / 16385 / 65530 / 65531 / 256 KB and records
  `put_packet()`'s error, the channel state and delivery; see §2 F1.
- **Leak check**: 50 connect/disconnect cycles, watching `IDHandler` growth via
  `evaluate_script` and the wasm heap.
- **Browser matrix**: Chrome, Firefox, Safari. Non-optional — Firefox has
  already produced two fork-specific blockers (`TASKS.md` Tasks 38, 41) and its
  WebRTC stack diverges from Chrome's more than its WebGPU one does.
- **Build matrix**: `threads=no` and `threads=yes` are **DONE — identical, see
  §4.2**; `dlink` remains untested (it needs the `misc/emsdk_patches/` toolchain
  patches, unlike threads-without-dlink).

### Phase 4 — Fix, integrate, document (2-3 days)

- Fix what Phases 2-3 confirm. **F1, F2 and F3 are done** — all inside
  `webrtc_data_channel_js.{h,cpp}` and `library_godot_webrtc.js`, plus one
  `ProjectSettings.xml` doc change for F3, which was the only one with real
  design content. F4, F5 and F7 are one-line JS fixes behind the same
  `null`-vs-`undefined` mechanism.
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
fixes, F1's behavioral test, F2 at the multiplayer layer,
`MultiplayerAPI`/`@rpc`, the dlink variant, and the Firefox/Safari matrix.

---

## 6. Explicitly out of scope

**Not the same as deferred.** Everything below is work this project will not
do. What needs a shaped or off-box link (§4.4) is *deferred* — worth doing,
just not doable in an in-process loopback.


- **`set_default_extension()` stays a no-op on web.**
  `webrtc_peer_connection.cpp:49-51` hardcodes the JS implementation under
  `WEB_ENABLED`, so the default-extension mechanism is dead code there. This is
  *correct* behavior — there is no web build of `webrtc-native` to load, and
  the JS implementation is what you want regardless. It needs one documented
  line so nobody debugs it twice.
- **Media tracks (audio/video).** See §1.
- **A libdatachannel-in-WASM port.** See §1.
- **Browser-imposed limits that cannot be closed**: no DTLS certificate or
  fingerprint control, no local port-range control, no mDNS candidate control.
  None of these are exposed in Godot's API on any platform anyway, so they do
  not affect parity — they only need recording in the parity table as
  "N/A both sides."

---

## 7. Progress log

All of the below is 2026-10-12 and 2026-10-13. 2026-10-12, on `webgpu-4.8`:
`1151998221` (evaluation + loopback harness), `58f98d01b0` (multiplayer layer),
`e8e969fb95` (threads + F3 measurement), `4e7919e241` (F1 + F2 tested and
fixed), `5398356e71` (F3), `15c7a1ccc3` (F8) and `5a00ef9df3` (F5/F7/F4).
2026-10-13 closed the three remaining coverage gaps — `@rpc`, dlink and the
browser matrix — and the tier became a CI stage.

### Status at a glance

| area | status |
|---|---|
| Feasibility evaluation | **DONE** — nothing to implement; see §1 |
| Reference peer (`webrtc-native`) builds + loads on 4.8 | **DONE** — §3 |
| Phase 0 web baseline | **DONE** — works end-to-end; §4 |
| Channel-layer parity table (18 observables) | **DONE** — 15 match; §4 |
| `WebRTCMultiplayerPeer` layer | **DONE** — parity but for F5; §4 |
| `threads=yes` | **DONE** — byte-identical to nothreads; §4.2 |
| F3 measurement | **DONE** — 65/500 web vs 500/500 native; §2 |
| F1 behavioral test | **DONE** — `tests/sizes.gd`, 8 payload sizes; §2 F1 |
| F1 fix | **DONE** — 16384 + `put_packet()` reports send failures |
| F2 at the multiplayer layer | **DONE** — `tests/mesh_close.gd`; §2 F2 |
| F2 fix | **DONE** — guard dropped, `close()` keeps the queue; `mesh_close` identical |
| F3 fix | **DONE** — buffer grows instead of dropping; `burst` 500/500, `grow` identical |
| F3 regression guard | **DONE** — `tests/grow.gd`, 2100 packets over 3 grow/regrow rounds |
| Fixes for F4, F5, F7 | **DONE** — one root-cause change in `get_prop` (`null` is absence) plus a default audit; `loopback`, `mesh` and `chancfg` became identical |
| F8 (`maxPacketLifetime` typo) | **DONE** — found by code review, not by the harness; measured by `tests/chancfg.gd` and fixed |
| `MultiplayerAPI` / `@rpc` | **DONE** (2026-10-13) — `tests/rpc.gd`, identical native/web; §7a |
| `dlink_enabled=yes` | **DONE** (2026-10-13) — third export variant, identical; §7a |
| Firefox | **DONE** (2026-10-13) — all 9 fixtures identical to native; §7a |
| Safari | **UNRUN** — driver written, macOS-only, never executed; §7a |
| CI integration | **DONE** (2026-10-13) — `run_tests.mjs`, a `local_ci.sh` stage; §7a |
| `get_buffered_amount()` (§4.3) | **RESOLVED — not a divergence**; `tests/buffered.gd`, `compare.sh buffered` identical |
| Behavior under real congestion (§4.4) | **DONE** (2026-10-13) — `webgpu_tests/webrtc_congestion/`, all four unknowns measured; produced F9 and F10; §4.4a |
| F9 (web's ~16 MiB send-queue cap) | **CONFIRMED, documented** — graceful and recoverable; no code change |
| F10 (web's unreliable modes lose 75-87% under loss) | **CONFIRMED, OPEN** — needs an upstream question about `unreliable_lifetime = 1`; no code change yet |

Estimate for the remainder: **nothing, except a Safari run (needs a Mac) and
the upstream conversation about F10's default.** The congestion work (§4.4) is
done and found two new divergences, F9 and F10. Every confirmed divergence is fixed (F1-F5,
F7, F8), the one unfiled suspicion (§4.3) is settled as a non-divergence, and
**all nine fixtures compare identical to native across three export variants and
two browsers — 9 x 3 x 2 = 54 clean diffs.**

### §7a Closing the coverage gaps (2026-10-13)

The three items Task 18.8 left open are closed, and **none of them found a
defect**. That is the result, not a disclaimer: each was a plausible place for a
divergence to hide, and the measurement says it does not.

**`MultiplayerAPI` / `@rpc` — `tests/rpc.gd`, identical.** This is the layer
projects actually write against, two levels above the `MultiplayerPeer` contract
every other fixture covers. Two `SceneMultiplayer` instances in one process,
each with its own `WebRTCMultiplayerPeer`, attached to two sibling subtrees via
`SceneTree.set_multiplayer(api, root_path)`, each holding a `Pong` node at the
same relative path — which is what lets an RPC resolve on the far side. It
covers `@rpc` in both directions, `get_remote_sender_id()`, a target-less
broadcast, and all three transfer modes. The modes matter more since F8: before
that fix both unreliable modes were secretly fully reliable, so this is the
first test to exercise channels 2 and 3 as configured. 26 lines, identical
native vs web, on every variant and both browsers.

**`dlink_enabled=yes` — a third export variant, identical.** Added as
`[preset.3]` "WebGPU-Dlink" (`extensions_support=true`, the nothreads dlink
template) exporting to `exports/loopback_dlink`, rather than as a one-off run,
so it stays covered. The fixture project *has* a GDExtension — the
`webrtc-native` reference peer — and it is excluded on web by its own
`exclude_tags = ["web", "no-webrtc"]`, so this also checks that an excluded
extension present in the project does not disturb the export. All nine fixtures
identical.

**Firefox — all nine fixtures identical to native.** The one result here that
was genuinely uncertain: Firefox's WebRTC stack diverges from Chrome's
considerably more than its WebGPU one does, and nothing in `modules/webrtc/`
had ever been run on it. It is at full parity. Only two observables differ
between the browsers at all, and both are normalized away as legitimately
variable: SDP length (`loopback`: Chrome 457/456, Firefox 492/491, native
444/444) and ticks to connect (Chrome 3, Firefox 13 — Firefox is slower to
gather candidates).
Every channel accessor, every transfer mode, F2's drain-after-close, F3's
2100-packet grow sweep and F8's channel configuration all read the same.

**Safari — the driver exists, the run does not.** `RTC_BROWSER=safari` drives
Safari through AppleScript, the same way `scene_smoketest/run_scenes.mjs` does,
and `local_ci.sh` runs it on `Darwin` unless `--no-safari`. It has **never been
executed**: this work was done on Linux and Safari does not exist there, and
Playwright's `webkit` is not Safari and has no WebGPU, so it is not a stand-in.
Treat a first Safari failure as a new data point, not a regression. What made it
possible to write at all was making the output readable without the console:
`rtc_test_base.gd` now mirrors every `RTC|` line onto `window.__rtc_lines`, and
the runner polls that array in every browser instead of scraping console
messages. AppleScript's `do JavaScript` cannot read a console, and polling an
array also prints each line exactly once — which the diff depends on.

**The tier is now a CI stage.** `run_tests.mjs` is the entry point: it runs
every fixture in every export present, in every browser requested, and asserts
what each fixture is supposed to *prove* rather than diffing — because the
native half needs the 581 MB `webrtc-native` clone, which a fresh checkout does
not have. 258 assertions over 9 fixtures x 3 variants per browser, 516 across both. A browser that cannot launch, or has no WebGPU adapter to
boot the engine on (Firefox on a GPU-less runner), is reported as a SKIP rather
than nine failures; a run where *nothing* could execute exits 2, so a suite
cannot report PASS on zero coverage.

Two harness bugs fell out of making it runnable from a clean state, both of the
"only ever worked on one machine" kind: `run_loopback.mjs` imported Playwright
by absolute path, and `export.sh` never created its output directories, so a
cleaned `exports/` failed the export with no useful message.

### Corrections made to this document as evidence arrived

Recorded because the first drafts were wrong in ways that reading alone could
not catch:

1. **F5's web value was predicted `65535`, measured `0`.** Read off the JS
   defaults; the defaults are unreachable. See §2 F5.
2. **F1's severity was overstated, twice.** First read: a real divergence
   (1200 vs 16384) but `get_max_packet_size` is not bound to GDScript, so it
   reaches C++ consumers only. Then the behavioral sweep showed the send path
   never enforced 1200 at all — Chrome delivers 256 KB payloads with
   `put_packet()` returning `OK`, same as native — so F1 was a *wrong reported
   number*, not a size limit. The size divergence that does exist is F3's
   receive buffer, at 65530 bytes.
3. **A claim that pre-open `id` agreed across platforms was wrong** — it
   compared native against the JS *default* rather than the measured web value.
   They differ (F7); it is post-open that agrees.
4. **A reported "hang" in the native reference peer did not exist.** The test
   script hit a `SCRIPT ERROR` before reaching `quit()`, so the `SceneTree`
   looped forever. Isolating it took two runs and found clean exits throughout.
5. **`get_buffered_amount()` was recorded as "web 500000 vs native 0", with
   native's 0 the suspicious half.** Four runs of the same fixture put native at
   0, 720000, 1580000 and 3430000 on the same tick: the quantity is
   nondeterministic and the original row was one sample of it. Both
   implementations report their own queue correctly and nothing needed fixing.
   The real finding underneath was different and smaller — the two ends'
   counters have no happens-before relation, so the value says nothing about
   what the peer holds (§4.3).
6. **"The multiplayer layer adds no divergences of its own" was true and
   misleading.** It does add a *bug* (F8), just not a divergence — both
   platforms were equally wrong, so a parity statement was literally correct
   while the layer was quietly ignoring half its own channel configuration. The
   wording was too comfortable; "no divergences" should never have been written
   without "which says nothing about whether it is correct".
7. **F3 was described as "can lose data if the application polls slowly".**
   True but far too mild: a single tick's burst loses 435 of 500 packets on a
   reliable channel, as a tail-drop with no API-level signal. It was also
   described as a defensible trade-off ("web bounds memory, native honors the
   guarantee, neither is obviously wrong") — but the bound was 64 KB, which one
   ordinary tick of traffic exceeds, and the project setting controlling it only
   ever applied to web. Growing the buffer costs nothing a desktop build does
   not already pay.

## 8. Lessons learned

### What worked, and is worth reusing

- **In-process loopback beats a signaling server.** Two peers in one process
  with SDP/ICE shuttled directly in GDScript needs no server and no second
  page, yet exercises the whole path — JS bridge, DTLS, SCTP, data channels. It
  is what made a day's progress possible instead of a week's.
- **One fixture, both targets, diffed.** The single highest-value decision.
  Because the same GDScript runs natively and in the browser and prints
  `RTC|`-prefixed lines, parity becomes `diff`, not judgment. `compare.sh`
  normalizes only what legitimately varies (object ids, class names, tick
  counts, SDP lengths) so **any surviving line is a divergence** — no
  interpretation required, and no divergence can be overlooked by a tired
  reader scanning two logs side by side.
- **Make each run self-documenting about its own configuration.** Printing
  `OS.has_feature("threads")` and `crossOriginIsolated` turned "I believe these
  are two different builds" into evidence. Cheap, and it preempts the entire
  class of embarrassment where a matrix turns out to have tested one thing
  twice.
- **Let the harness own its server.** Folding a COOP/COEP server into the
  runner removed a manual step *and* a silent failure mode, since a threaded
  export simply does not work without cross-origin isolation.
- **A dispatcher instead of one project per test.** `-- <name>` natively,
  `?test=<name>` on web. Three fixtures now share one project, one export
  pipeline and one set of presets.
- **The reference implementation is the oracle.** Once `webrtc-native` was on
  disk, three suspicions became confirmed facts in minutes by reading
  `WebRTCLibDataChannel.cpp` — far faster than designing experiments for each.

### Traps, each of which cost real time

- **A blocking loop can never work on web.** `SceneTree` + `while` +
  `OS.delay_msec` works natively and times out on web with no connection at
  all, because the JS bridge is callback-driven from the browser event loop.
  Drive fixtures from `_process()`. Same family as HANDOFF §4.11's yielding
  startup.
- **`--headless -s script.gd` does not generate `.godot/extension_list.cfg`.**
  Without an `--import` pass first the GDExtension is silently *not loaded* and
  `WebRTCPeerConnection.create()` quietly falls back to the base class with one
  `WARN_PRINT_ONCE`. This produced a convincing false negative that looked like
  "the extension does not work on 4.8".
- **For a mesh, `get_connection_status()` is `CONNECTED` immediately.** It
  reports on `create_mesh()` succeeding, not on any remote peer's channels
  being open. Gating the first send on it raced the channels:
  `ERROR: DataChannel not open`. Gate on `peer_connected`.
- **`webrtc-native` aborts the editor (exit 134) on a project's first
  filesystem scan** (F6). Harmless to results — `extension_list.cfg` is still
  written, later runs exit 0 — but it fails any CI step that checks exit codes.
- **godot-cpp's platform is `linux`, not `linuxbsd`,** and
  `build_profile=build_profile.json` must be passed or the build compiles the
  entire binding set for nothing.
- **A script error before `quit()` looks exactly like a hang.** Worth ruling
  out before investigating a crash in a C++ dependency.

### Epistemic lessons, which are the ones that generalize

- **Reading code predicts behavior badly at a language boundary.** F5 and F7
  were both mispredicted from the JS source, and for the same reason:
  `get_prop` fell back only on `undefined`, but browsers report unset channel
  properties as `null`, so documented-looking defaults (`65535`) were
  unreachable and `null` crossed into C++ as `0`. **Those fallback values were
  dead code and could not be read as documentation of behavior.** The fix is
  the interesting part: making `null` count as absence turned every default in
  the file live at once, so a one-line fix for F5 meant auditing all five
  defaults, fixed F7 for free, and *forced* F4 — a boolean defaulting to
  `65535`, harmless only because it was unreachable. **Dead code one condition
  away from running is not harmless, it is pre-loaded.**
- **A sentinel inside the value's legal range hides configuration, not just
  defaults.** F5 read as cosmetic for most of this work — `0` vs `-1` for an
  unset field. What made it real was `chancfg.gd` setting `maxRetransmits: 0`
  deliberately and finding web reporting `0` for that *and* for unset: a
  legitimate configuration indistinguishable from no configuration. Worth
  asking of any sentinel.
- **Measure before assigning severity, in both directions.** F1 was downgraded
  (not script-exposed), F3 and F5 were upgraded — F3 because a burst is
  catastrophic rather than lossy, F5 because the multiplayer-layer test showed
  it on channels *every* project creates. Three of seven findings had the wrong
  severity after a careful read.
- **A negative result is a result, and worth the same rigor.** "The
  multiplayer layer adds no divergences" and "threads changes nothing" are both
  load-bearing, and both only mean anything because the comparison was
  mechanical and the configurations were proven distinct.
- **State the gap when a test does not cover what it appears to.** The mesh
  fixture polls every tick, so it cannot reproduce F2 — recording that is more
  useful than letting a green mesh run imply F2 is confined to hand-rolled
  channels. Writing the missing `mesh_close.gd` then took twenty minutes,
  because the gap had already been written down precisely.
- **Test the data structure's hard case, not just the symptom.** The F3 fix is
  "grow the ring buffer", and `burst.gd` proves the symptom is gone — but it
  only ever grows a buffer that filled from empty. `RingBuffer::resize()` has a
  separate branch for growing *wrapped* data, where a mistake reorders or
  corrupts packets instead of losing them, and nothing in the existing fixtures
  reached it. `grow.gd` exists to: partial drains leave the read position mid
  buffer, then a larger batch forces a wrapped grow, three times, with global
  sequence numbers checked for loss, duplication and reordering.
- **Hold a reference to what the API under test is about to throw away.**
  `mesh_close.gd` keeps a `Ref` to the channel taken while connected, so when
  `WebRTCMultiplayerPeer::poll()` drops the peer it can still ask the channel
  whether the packets survived. Without that second read, both platforms show
  "LOST" and the fix looks ineffective; with it, the platform-independent
  structural loss separates cleanly from the web-only one that was the actual
  bug. **When a layer discards the object you are measuring, measure the object
  directly as well as through the layer.**
- **A cross-platform diff cannot see a bug both platforms share.** F8 — the
  `maxPacketLifetime` typo that made both unreliable transfer modes reliable
  everywhere — was invisible to `compare.sh` by construction: native and web
  agreed, so the line matched and the tier reported parity. It took a human
  reading the shared code. **The harness answers "do these differ?", never "is
  either correct?"**, and anything in `modules/webrtc/`'s platform-agnostic
  files is outside what it can check. Worth re-reading the shared layer
  deliberately rather than trusting a green diff.
- **The evidence can be sitting in your own output, unread.** F8's fingerprint
  was in the §4 mesh table from the first day: channels labeled `ordered` and
  `unreliable` reported `lifetime` *unset*, which cannot happen if their config
  took effect. It was filed under F5 (how *unset* prints) and the obvious
  follow-up question — why is it unset at all? — went unasked for three
  commits. Reading a table for the difference you went looking for is not the
  same as reading it.
- **Check who actually reads a setting before treating it as a constraint.**
  F3's 64 KB cap came from `network/limits/webrtc/max_channel_in_buffer_kb`,
  documented as a maximum, which made "web bounds memory on purpose" sound like
  a design position worth preserving. But the base class that reads the setting
  is only used by the web implementation — the extension keeps its own queue and
  ignores it — so the "trade-off" was a web-only accident, not a cross-platform
  policy. That turned a hard design question into an easy one.
- **A limit that is only reported can look exactly like a limit that is
  enforced.** F1's 1200 was read as "web refuses larger packets"; the sweep
  showed web sends 256 KB happily and the real ceiling is 65530 bytes, in a
  different component, for a different reason (F3's receive buffer). The fix
  for a wrong number is one line; the fix for the enforced limit it was
  mistaken for would have been a design change.
- **Do not promote an unsound comparison to a finding.** The
  `get_buffered_amount()` difference (500000 vs 0) looked real and would have
  mattered, but it was read incidentally at an uncontrolled moment, so it was
  logged as needing a proper test (§4.3) rather than filed as F8. **That call
  paid off**: the proper test showed native returning 0, 720000, 1580000 and
  3430000 across four identical runs. Had it been filed as F8 it would have been
  "fixed" — by changing code that was already correct.
- **Sample a value more than once before believing it.** One reading of a
  nondeterministic quantity is an anecdote. The cheapest possible check — run
  the same fixture four times — was what dissolved §4.3, and it cost less than
  writing the finding up would have.
- **When an assertion fails, check whether it was ever an invariant.**
  `buffered.gd` first asserted that a sender cannot owe more than the receiver
  is missing. Web failed it by ~61 KB, which looked like a bug and was not: the
  two counters are updated independently, so the relation never held in the
  first place. The fix was to demote the check to recorded data — and that
  demotion *is* the finding worth keeping, since it means
  `get_buffered_amount()` tells a project nothing about its peer.
- **Check whether the work is already done before planning it.** The whole
  original question — "what would it take to add native WebRTC at parity?" —
  had the answer "it is already there, and here are the seven ways it differs".
  An hour of reading `modules/webrtc/` and the `bin/` artifacts was worth more
  than any amount of implementation planning.
