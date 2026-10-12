# WebRTC congestion tier

Measures what WebRTC does when the **link cannot keep up with the application**
— the one class of behavior the parity tier
(`webgpu_tests/webrtc_parity`) deliberately leaves untested, because every
fixture there runs over an unthrottled in-process loopback.

Background and the four questions this answers: `webgpu_notes/webrtc_web.md`
§4.4 (the deferral) and §4.4a (the results). Status: `TASKS.md` Task 18.12.

**This tier found two new divergences, F9 and F10.** F10 is the more serious:
on web, Godot's unreliable transfer modes lose **60–85%** of packets on a lossy
link where desktop loses 5–8%. See "What this found" below.

## How it differs from the parity tier

| | parity tier | this tier |
|---|---|---|
| peers | two in one process/page | two **processes**, one per side of a shaped link |
| signaling | none — SDP shuttled in GDScript | a real WebSocket signaling server |
| link | loopback, unshaped | `netem`: rate, delay, loss |
| question | "do web and native agree?" | "what happens when the link is the bottleneck?" |
| verdict | `diff` of two logs | each fixture's own verdict, from both ends |

Because the peers are separate processes, this tier can also do something the
parity tier never could: run **web against native** — a browser peer on one
side of the link and a libdatachannel peer on the other.

## Shaping, and why it needs no root

`shape.sh` builds a two-namespace topology joined by a veth pair with `netem`
on both ends:

```
  ns A (this process)                      ns B (ip netns exec rtcb)
  10.77.0.1  ──[ netem rate/delay/loss ]── 10.77.0.2
```

It runs under `unshare -rnm`, which maps the invoking user to root inside a new
user + network + mount namespace and so grants `CAP_NET_ADMIN` **there only**.
Every interface, address and qdisc lives inside that namespace and vanishes
when the command exits.

This matters beyond convenience. The approach originally sketched in §4.4 was:

```bash
sudo tc qdisc add dev lo root netem rate 1mbit delay 50ms   # DON'T
```

which needs root and rate-limits **all** loopback traffic on the machine for as
long as it is applied — so an interrupted run leaves the host shaped, and
nothing else can run meanwhile. `shape.sh` cannot do either.

Requires unprivileged user namespaces (`/proc/sys/kernel/unprivileged_userns_clone`
= 1; the default on Ubuntu/Zorin).

## Running

```bash
# 0. Prove the shaping actually works before trusting any result from it.
./shape.sh --selftest --rate 2mbit --delay 25ms
#    -> RTT ~50ms (2x the one-way delay) and ~1.9 Mbit/s of throughput

# 1. Native peers, both sides, over a 2 Mbit/s link:
./shape.sh --rate 2mbit --delay 25ms -- node run_congestion.mjs

# 2. A browser on one side (needs ./export.sh first):
./export.sh
./shape.sh --rate 8mbit --delay 10ms -- env RTC_A=web RTC_TEST=send_ceiling node run_congestion.mjs

# 3. The whole matrix, tabulated (~15 min):
./sweep.sh
./sweep.sh loss_modes      # one fixture
```

`RTC_A` / `RTC_B` choose each side independently: `native` (default), `web`, or
`external` (another machine — see below). `RTC_TEST` picks the fixture.

**Started without `shape.sh`, the runner says so and both peers fall back to
loopback.** That unshaped mode is a deliberate control, not an error: it is what
shows the fixtures report `NO_PRESSURE` when nothing is shaping, i.e. that a
shaped result is not an artifact of the fixture.

## Fixtures

| fixture | the §4.4 question it answers |
|---|---|
| `backpressure` | **1.** Does native's `get_buffered_amount()` *track* a real backlog — rise under load, fall as the link drains — or only flicker? It is the only flow-control signal in Godot's WebRTC API. |
| `send_ceiling` | **2.** Does web hit a send-queue cap that native does not, and is hitting it graceful (`FAILED`, channel survives, retry works) or fatal? |
| `recv_growth` | **3.** F3 made web's receive buffer grow instead of dropping. How large does it get with a peer that never drains, does the 8 MiB warning fire usefully, and does shrink-on-empty run? |
| `loss_modes` | **4.** Do the transfer modes actually *behave* differently under loss? The parity tier compares their configuration but never their behavior — which is exactly how F8 survived a clean diff. |

Add one as `fixtures/congestion_project/tests/<name>.gd` extending
`tests/cong_base.gd`, which owns signaling, the offer/answer/ICE exchange and
the per-tick poll. Implement `_create_channels()`, `_on_data_channel()`,
`_step()` and `_channels()`.

## Rules for new fixtures

1. **Never block the main loop** — `extends Node`, drive from `_step()`. Same
   reason as the parity tier: on web the JS bridge is callback-driven from the
   browser event loop, so a blocking loop starves the callbacks that would have
   drained the queue.
2. **Never use a tick count as a timeout.** This is new here and it is the rule
   most easily got wrong: a congested run is slow *by design*, ticks are frames,
   and frames are not time. Use `now()`, which is wall-clock seconds, and size
   every budget off the shaped rate.
3. **Give every fixture a `NO_PRESSURE` verdict.** Without one, a forgotten
   `shape.sh` looks exactly like a negative result. §4.4's own caveat — "verify
   with the delivery counts, not by assumption" — made structural.
4. **Pace the stream when the question is about loss, not queueing.** A
   saturated link makes every channel lose packets and proves nothing about
   transfer modes. `loss_modes` sends at 80 KB/s over a 4 Mbit/s link for this
   reason, and uses the reliable channel as a control: if *it* loses anything,
   the run is thrown out (`CONTROL_LOST_DATA`).
5. **Report from both ends.** The answerer sends its counts back over the
   signaling socket so one verdict is computed from both sides, rather than
   leaving a reader to cross-reference two logs.

## Off-box peer (two real machines)

Validated 2026-10-13: a Chrome peer on one machine against a native
libdatachannel peer on another, over a real LAN. Useful as a control that
nothing here is a veth artifact.

```bash
# On the machine running the test (A), bind signaling to a LAN address:
RTC_SIGNAL_IP=<A's LAN IP> RTC_A=web RTC_B=external RTC_TEST=loss_modes \
    node run_congestion.mjs
# It prints the exact command to run on machine B. Copy the project + the
# editor binary there, import once (exit 134 is expected — F6), then run it.
```

**A clean LAN applies no pressure**, so this returns
`MODES_INDISTINGUISHABLE` — which is itself worth knowing: **F10 is invisible
on a LAN and appears only once a link drops packets.** A developer testing on
a LAN will never see it; a player on wifi or mobile will. Getting real pressure
across two machines needs `netem` on one of the NICs, i.e. root on one of the
machines, which `shape.sh` deliberately avoids.

## What this found

Full detail and the per-run numbers: `webgpu_notes/webrtc_web.md` §4.4a.

| § | question | answer |
|---|---|---|
| 1 | native's `get_buffered_amount()` | **It tracks.** Peak 602 KB on a 2 Mbit/s link, rises under load, returns to 0. Usable for flow control. |
| 2 | web's send-queue cap | **F9 — real divergence.** Chrome refuses past **16,770,584 bytes (~16 MiB)**; native accepted 24 MB with no cap. **Graceful**: `put_packet()` returns `FAILED`, the channel stays open, and a retry after drain succeeds. |
| 3 | F3's receive-buffer growth | **Sound.** Held 12 MB with nothing drained, 3000/3000 intact, no reordering, shrink-on-empty ran. The 8 MiB warning fires at exactly 8,388,608 bytes and names the fix. (The "no ceiling" in §4.4 is not quite right: there is a hard refusal at 1 GiB, `IN_BUFFER_MAX_SHIFT`.) |
| 4 | transfer modes under loss | **F10 — the serious one.** See below. |

### F10: web's unreliable transfer modes lose most of their packets

At 5% link loss and 25 ms one-way delay, delivery of 200 packets per channel:

| channel | config | native | web (Chrome) |
|---|---|---|---|
| `reliable` | `{ordered:true}` | 100% | 100% |
| `rtx0` | `+ maxRetransmits:0` | 94–97% | 91–95% |
| `life50` | `+ maxPacketLifeTime:50` | 93–97% | 94–97% |
| **`life1`** | `+ maxPacketLifeTime:1`, ordered | **90.5–95.5%** | **28.5–42.5%** |
| **`life1u`** | `+ maxPacketLifeTime:1`, unordered | **92.5–99%** | **13–25.5%** |

The two `life1*` rows are not hypothetical: they are byte-for-byte what
`WebRTCMultiplayerPeer::add_peer` builds for `CH_ORDERED` and `CH_UNRELIABLE`,
whose `unreliable_lifetime` parameter **defaults to 1** (ms). So on web,
`TRANSFER_MODE_UNRELIABLE` delivers ~13–25% of its packets where desktop
delivers ~95%.

Three things make this readable rather than alarming:

- **A 1 ms lifetime does not cause loss by itself.** On a lossless 25 ms link
  every channel delivers 100% on both platforms, because the lifetime governs
  *retransmission*, not first transmission. It bites only once something drops.
- **`life50` is the control, and it agrees across platforms.** The divergence
  belongs to the lifetime *value*, not the mechanism — which also means the
  mitigation is already proven: pass a larger `unreliable_lifetime` to
  `add_peer()`.
- **It is a consequence of fixing F8, not a regression from it.** Before that
  one-character fix the lifetime was never applied at all, so both unreliable
  modes were silently *fully reliable* everywhere. The fix made them honest;
  this measures what "honest" costs on each platform.
