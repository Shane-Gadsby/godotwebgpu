# A Node.js Dedicated-Server Backend for Godot WebGPU Exports — Evaluation and Scope

**Status:** Evaluation complete. **Verdict: feasible and worth building — but not
as "a geckos.io server".** Build a Node orchestrator plus a transport that
speaks *Godot's own* `WebRTCMultiplayerPeer` convention, and keep authoritative
simulation in headless Godot. Full reasoning in §5, plan in §7.
**Date:** 2026-10-12
**Branch at time of writing:** `webgpu-4.8` (`cc9fd1cc62`)
**Companion docs:** `webgpu_notes/webrtc_web.md` — the web/native WebRTC parity
work in flight in a parallel session. `webgpu_notes/geckos_backend_schedule.md`
— the full week-by-week implementation schedule (§7 here is its summary).
**§11 answers "how much better off would a Node-side simulation make me?"**
with measured numbers, and supersedes §5.4's effort estimate.
**Everything here depends on the parity doc's findings**, especially F3
(§6.2) — read that one first.

**Nothing here has been built or run.** Every claim is labeled: *verified*
(read from source or measured on this machine), *documented* (upstream docs /
npm registry), or *inferred* (reasoned, needs a Phase 0 spike). The inferred
items are listed again in §9 so none of them silently becomes an assumption.

---

## 1. Executive summary

The question asked was whether a Node.js + geckos.io server is a sensible
multiplayer backend for Godot WebGPU web exports. Short answer:

1. **The transport geckos.io uses is exactly right.** WebRTC data channels are
   the only way a browser gets UDP-like semantics, Godot already implements
   them on *both* web and native (`modules/webrtc/`), and this fork has just
   measured that implementation to be at parity on 17 of 18 observables
   (`webrtc_web.md` §4). The platform question is settled before we start.
2. **geckos.io's *framing* is wrong for Godot, and adopting it costs you the
   entire engine multiplayer stack.** geckos puts a single unreliable,
   unordered channel on the wire, frames every message as the JSON string
   `{"eventName": payload}`, and implements "reliable" as *sending the same
   message ten times, 150 ms apart, and de-duplicating by a random id*
   (verified, §2.4). Godot's `WebRTCMultiplayerPeer` instead opens three
   pre-negotiated channels with distinct reliability and sends **raw,
   unwrapped payloads** (verified, §2.6). Speak geckos' dialect and you give up
   `@rpc`, `MultiplayerAPI`, `MultiplayerSpawner` and `MultiplayerSynchronizer`,
   and you hand-roll everything they do. That is the single most consequential
   finding in this document.
3. **The valuable 20% of geckos.io is its signaling server**, and that part is
   ~250 lines of plain HTTP that we can either reuse as a protocol shape or
   lift outright (§2.2). Its other real contribution — single-UDP-port ICE
   muxing — comes from `node-datachannel`, the library geckos itself is built
   on, which we can use directly.
4. **Therefore: build on `node-datachannel`, not on geckos.io**, and make the
   Node server present itself to Godot as a *stock Godot WebRTC peer*
   (three negotiated channels, ids 1/2/3, no framing). The Godot client then
   needs **no new transport code at all** — stock `WebRTCMultiplayerPeer`
   plus ~150 lines of GDScript signaling — and `@rpc` keeps working. This is
   the "patched into the normal multiplayer systems" outcome asked for, and it
   is available at Tier 1 effort (§5.3).
5. **Keep authoritative game simulation in headless Godot, not in Node**, unless
   there is a specific reason to want gameplay logic in JS. Server-authoritative
   `@rpc` *originating in Node* requires reimplementing Godot's variant
   binary encoding and the `SceneMultiplayer` command protocol in JavaScript
   (§5.4) — 2-3 extra weeks and a permanent compatibility tax on every engine
   release. Node's job is the control plane: auth, matchmaking, rooms, server
   process lifecycle, signaling. That is work Godot is bad at and Node is good
   at, and it is where the previous attempt's real value lived.

**Recommended shape:**

```
                browser (Godot WebGPU export)          native build (same GDScript)
                        │                                        │
         HTTPS: auth, matchmaking, room list, signaling ─────────┤   control plane
                        │                                        │   (reliable, Node)
          ┌─────────────▼────────────────────────────────────────▼──────────┐
          │  Node orchestrator: accounts, matchmaking, room registry,       │
          │  HTTP signaling (geckos-shaped), game-server supervision        │
          └─────────────┬───────────────────────────────────────────────────┘
                        │ spawns / assigns
          ┌─────────────▼───────────────────────────────────────────────────┐
          │  authoritative sim: headless Godot (recommended)                │
          │  — or a Node sim if JS gameplay logic is genuinely wanted       │
          └─────────────┬───────────────────────────────────────────────────┘
                        │ WebRTC data channels, Godot's own convention
                        ▼                              data plane (unreliable, UDP)
            WebRTCMultiplayerPeer → MultiplayerAPI → @rpc / synchronizers
```

**Cost:** ~3-4 weeks to a production-shaped stack on the recommended path;
~6-7 weeks if the simulation must live in Node. Phase breakdown in §7.

**When *not* to do this at all:** if the target games are peer-to-peer or
host-authoritative with ≤8 players and cheating does not matter, Godot's
existing `WebRTCMultiplayerPeer` mesh plus a ~100-line signaling server is
already enough, and everything below is overkill. The case for this project is
*server authority* (anti-cheat, authoritative physics, persistence,
late-join, headless match hosting) — not connectivity.

---

## 2. The technology, broken down

### 2.1 What geckos.io is

*Documented + verified against the vendored copy in
`/mnt/109313D2109313D2/unity-game-dev/multiplayer-maze/server/node_modules/@geckos.io/`
(v3.0.0) and the current tarball from npm (v3.1.0).*

A `socket.io`-shaped client/server library whose transport is a WebRTC data
channel instead of a WebSocket, so browser↔server traffic gets UDP semantics.
Three packages: `@geckos.io/client` (browser), `@geckos.io/server` (Node),
`@geckos.io/common` (framing shared by both). Sibling projects:
`@geckos.io/snapshot-interpolation` (client-side interpolation and a snapshot
model) and `@geckos.io/phaser-on-nodejs`.

**Maintenance (verified against the npm registry, 2026-10-12):**

| package | latest | published | notes |
|---|---|---|---|
| `@geckos.io/server` | **3.1.0** | **2026-03-27** | ESM-only, `engines: node >=18` |
| `node-datachannel` (its dep) | 0.33.4 | 2026-09-12 | geckos 3.1.0 pins **0.32.1** |

Both are alive; `node-datachannel` is the more actively released of the two.
geckos is a small-maintainer project (one primary author, ~1.5k stars, releases
roughly annually), which is a supply-chain consideration, not a disqualifier —
and it matters much less under the recommendation here, where geckos is a
*reference protocol* rather than a runtime dependency.

`node-datachannel` wraps **libdatachannel** — the same C++ stack the official
`webrtc-native` GDExtension uses for Godot's desktop WebRTC
(`webrtc_web.md` §3, where it was built and verified on this machine at
libdatachannel v0.24.6). Both ends of the recommended architecture therefore
sit on the same ICE/DTLS/SCTP implementation, which is a real diagnostic
convenience: one stack's bug classes, not two.

### 2.2 The signaling protocol (verified by reading the client and server)

This is the part worth copying, and it is pleasantly small. The **server is the
offerer** and creates the data channel; the client answers. All over plain HTTP
under `{url}/.wrtc/v2`:

| # | call | body / response |
|---|---|---|
| 1 | `POST /connections` (optional `Authorization` header) | → `{ userData, id, localDescription: { type: "offer", sdp } }` |
| 2 | `POST /connections/{id}/remote-description` | client's answer, `{ type, sdp }` → 200 |
| 3 | `GET /connections/{id}/additional-candidates` | → array of the server's trickled ICE candidates; drains the server-side list |
| 4 | `POST /connections/{id}/close` | → 200 |

`id` is a 24-char alphanumeric token. Step 3 is polled on a backoff schedule
(10 attempts, `50 * 1.8^n` ms plus up to 20 ms jitter — so ≈50 ms to ≈13 s),
started *before* the answer is sent, because server candidate gathering can
take seconds.

Three properties of this protocol matter a lot for us:

- **`userData` is an arbitrary JSON object the server attaches at connect time**,
  derived from the `Authorization` header by the server's auth hook. It is the
  natural place to carry **the Godot peer id the server assigns** — which is
  exactly the one piece of information `WebRTCMultiplayerPeer.create_client()`
  needs and cannot invent for itself. The protocol already has the hole our
  design needs.
- **It is implementable from GDScript with `HTTPRequest` alone** — no JS, no
  `JavaScriptBridge`, no addon — and therefore the *same* GDScript runs in the
  browser and in a native build. This is the single biggest difference from the
  previous Unity attempt (§4).
- **Client ICE candidates have nowhere to go.** There is no endpoint for the
  client's trickled candidates; the client POSTs its answer immediately after
  `setLocalDescription`, i.e. before gathering completes, so the answer is
  effectively candidate-free. Connectivity then relies on the server learning
  the client's address as a **peer-reflexive candidate** from inbound
  connectivity checks. *Inferred:* that works for a server with a public IP and
  is presumably how geckos works in browsers today, but it means **a
  TURN-only client cannot connect** without adding a client-candidate endpoint.
  Flagged as Phase 0 test G3 (§9).

### 2.3 Wire format (verified, `@geckos.io/common`)

- **Event messages** are the JSON string `{"<eventName>": <payload>}`; the
  receiver takes the first key as the event name. One channel carries all
  events, discriminated by that key.
- **Raw messages** (`channel.raw.emit`) are strings or `ArrayBuffer`/
  `ArrayBufferView`, delivered as the `rawMessage` event.
- The discriminator between the two is **"does `JSON.parse` succeed"**, with a
  carve-out: a string that parses as a number is treated as raw
  (`isJSONMessage` returns false if `!isNaN(parseInt(data))`). A binary payload
  that happens to be valid UTF-8 JSON would be misrouted. Workable, but it is a
  heuristic where Godot has a channel id.
- `maxMessageSize` is read off the live data channel at open
  (`dataChannel.maxMessageSize()`), and `SendMessage` throws above it.

### 2.4 The reliability model (verified — the most important mismatch)

The geckos server channel is created with `ordered: false, maxRetransmits: 0`
(`server/lib/wrtc/connectionsManager.js:50`), i.e. **one fire-and-forget
channel**. `reliable: true` on an emit does *not* switch to a reliable channel —
`makeReliable()` sends the same message **`runs = 10` times at
`interval = 150` ms** with a shared 24-char random id, and the receiver
discards duplicates it has already seen.

For small, idempotent control messages over a lossy link that is a legitimate
trick. As a general reliability layer it is not: 10× bandwidth amplification,
up to 1.35 s of tail latency, no ordering, no flow control, and no
back-pressure signal. Godot's `WebRTCMultiplayerPeer` gets real
SCTP-level reliability from channel 1 for free. **Adopting geckos' framing
would mean replacing a correct reliability layer with a worse one.**

Also verified: `autoManageBuffering` (default on) **drops** messages rather
than queueing when the channel buffer fills, reporting via `onDrop`. Note the
symmetry with this fork's F3 finding (§6.2) — both ends of a geckos link drop
under back-pressure. Godot's web receive path drops *silently*; geckos at least
tells you.

### 2.5 Server-side features and deployment constraints

*Documented, with the options verified in the vendored source.*

- **Rooms:** `channel.join(id)` / `leave()`, `channel.room.emit()`,
  `channel.broadcast.emit()`, `channel.forward(room).emit()`,
  `io.room(id).emit()`. Lightweight and in-process only — no cluster support,
  no cross-process fan-out, no persistence. Any multi-process deployment needs
  its own room registry regardless.
- **Ports:** TCP for signaling (9208 by default) and historically UDP
  `1025-65535` for the peer connections — which is why geckos' own docs say it
  cannot run on PaaS hosts that will not open UDP ranges. **`multiplex: true`
  is the default in v3** and sets libdatachannel's `enableIceUdpMux`, so all
  connections share **one** UDP port. That single flag is what makes this
  deployable in a container or behind a normal firewall, and it is a comparatively
  recent capability — any older write-up claiming "needs 64k UDP ports open" is
  out of date.
- **ICE:** ships a public STUN list "for testing"; production wants your own
  STUN/TURN. A dedicated server with a public IP needs neither for most clients
  (its host candidate is directly reachable), but **CGNAT and
  corporate-firewall clients will need TURN over TCP/443**, which is a real
  hosting line item and a latency cliff for the clients that hit it.
- **TLS:** a browser page served over HTTPS cannot signal to an `http://`
  endpoint (mixed content), so the signaling endpoint needs a valid
  certificate. Not optional, and easy to forget until the first non-localhost
  test.
- **Threading:** a Node process is single-threaded; libdatachannel does its ICE/
  DTLS/SCTP work on its own threads, but all JS game logic is one core. Scaling
  is horizontal (one process per N rooms), which is the orchestrator's job.

### 2.6 What Godot's `WebRTCMultiplayerPeer` actually puts on the wire

*Verified by reading `modules/webrtc/webrtc_multiplayer_peer.cpp` in this
checkout. This is the specification the Node server has to meet, and it is
much simpler than expected.*

For every peer, `add_peer()` creates exactly three data channels
(`:304-321`), all with `negotiated = true` — meaning **both sides create them
independently with matching ids and no `datachannel` event is exchanged**:

| id | label | config | Godot transfer mode |
|---|---|---|---|
| 1 | `reliable` | `ordered: true` | `TRANSFER_MODE_RELIABLE` |
| 2 | `ordered` | `ordered: true`, `maxPacketLifetime: <add_peer arg, default 1>` | `TRANSFER_MODE_UNRELIABLE_ORDERED` |
| 3 | `unreliable` | `ordered: false`, same lifetime key | `TRANSFER_MODE_UNRELIABLE` |

Extra channels from `channels_config` take ids 4+ (`CH_RESERVED_MAX + i + 1`).

And critically: **`put_packet()` writes the payload unmodified**
(`:377-414`) — no peer id, no sequence, no length prefix, nothing. Identity is
*which connection* a packet arrived on; transfer mode is *which channel id*.
`get_packet()` round-robins peers and drains channels in order
(`:359-375`).

So "be a Godot peer" means, in full: open three negotiated channels with ids
1/2/3 and matching ordering, send raw bytes, and tell the client its peer id
out-of-band. **That is it.** `node-datachannel`'s `DataChannelInitConfig`
exposes `{ negotiated, id, ordered, maxPacketLifeTime, maxRetransmits }`
(verified in its `lib/index.d.ts`), so all three are directly constructible.

### 2.7 A bug found while verifying §2.6 — `maxPacketLifetime` is misspelled

*Verified, upstream, platform-independent, and relevant to any snapshot-rate
design.*

`webrtc_multiplayer_peer.cpp` writes the key **`maxPacketLifetime`** (lowercase
`t`) at `:223`, `:226` and `:314`. Both consumers read **`maxPacketLifeTime`**:

- native: `WebRTCLibPeerConnection.cpp:119-121` tests
  `p_dict.has("maxPacketLifeTime")`;
- web: the dict is handed to the browser's `createDataChannel()`, which honors
  `maxPacketLifeTime` and ignores unknown keys.

Nothing reads the key Godot writes. **Channels 2 and 3 therefore get no packet
lifetime on any platform, making `TRANSFER_MODE_UNRELIABLE_ORDERED` fully
reliable-ordered and `TRANSFER_MODE_UNRELIABLE` unordered-but-reliable.**
A dropped snapshot is retransmitted until it arrives, which is precisely the
wrong behavior for state that the next snapshot supersedes — under loss it
converts a hiccup into head-of-line delay.

Consequences for this project, and why it is in *this* doc:

- It is a one-character upstream fix (`modules/webrtc/webrtc_multiplayer_peer.cpp`),
  worth landing and upstreaming via the parallel WebRTC session.
- Until it is fixed, **our Node server must not copy Godot's key**: set
  `maxPacketLifeTime` (correct spelling) on channels 2 and 3 server-side so at
  least the server→client direction behaves as designed.
- It interacts badly with F3 (§6.2): reliable retransmission into a 64 KB
  silently-tail-dropping receive buffer is the worst of both worlds.
- **It should be measured, not just read.** `webgpu_tests/webrtc_parity/` can
  do this today: send on channel 3 across a lossy link and check whether
  packets are retransmitted. Suggested to the parallel session as an F8.

---

## 3. Fit for purpose

### 3.1 What a Node backend genuinely buys this project

- **Dedicated-server topology at all.** Web exports cannot listen on a socket,
  so "host a game" from a browser is impossible; something off-browser has to
  be the authority. (`ENetMultiplayerPeer` is natively unavailable on web, and
  `WebSocketMultiplayerPeer` works but is TCP — head-of-line blocking on every
  snapshot.) The realistic choices are a WebRTC server or a WebSocket server;
  this doc is about the former.
- **Server authority.** Anti-cheat, authoritative movement/physics, server-side
  spawning, persistence, late join, replay. The previous attempt (§4) did not
  have this, and its git history shows the cost.
- **The control plane.** Accounts, matchmaking, room registry, invite codes,
  presence, per-match server lifecycle, metrics. Node's ecosystem here is
  overwhelming relative to GDScript's, and this is work the engine will never
  do for you.
- **One deployable artifact for web and native clients.** Because the signaling
  is HTTP and the transport is Godot's own, the same client code works on both
  targets — which this fork cares about more than most.

### 3.2 Where geckos.io specifically fits — and does not

| geckos.io gives you | fit | notes |
|---|---|---|
| WebRTC data channel transport | ✅ right call | but Godot already has the client half, natively, on both targets |
| HTTP signaling server | ✅ **the keeper** | small, CORS-able, GDScript-implementable; reuse the protocol shape (§2.2) |
| single-UDP-port ICE mux | ✅ essential | but it comes from `node-datachannel`, not geckos |
| rooms / broadcast helpers | 🟡 thin | in-process only; ~40 lines to reimplement; needed anyway for multi-process |
| `{event: payload}` JSON framing | ❌ **costs you `@rpc`** | §2.4, §2.6 — the deal-breaker |
| "reliable" = send 10× | ❌ strictly worse | Godot gets real SCTP reliability free |
| `snapshot-interpolation` | 🟡 nice reference | its *model* (snapshot buffer, interpolation, quantization) is worth stealing; its JSON-ish wire isn't |
| back-pressure management | ✅ good idea | and more honest than Godot's silent web drop (§6.2) |

The honest summary: **geckos.io is a very good answer to "I am writing both ends
in JavaScript."** We are not. Godot is the client, and Godot already contains a
better-specified version of the two things geckos' *protocol* provides
(multi-channel reliability, framing). So we take geckos' architecture and its
signaling shape, build on its own foundation (`node-datachannel`), and skip its
framing. Running the stock geckos server anyway in Phase 0 is still valuable —
as a known-good peer to prove the GDScript handshake against before we write
any server of our own (§7, Phase 0).

### 3.3 The alternative that must be ruled out first: headless Godot as the server

Before committing to Node, state the competing option plainly: **export the game
as a headless Linux binary, run it as the authoritative server, and use
`WebRTCMultiplayerPeer.create_server()`.** Then `@rpc`, `MultiplayerSpawner`,
`MultiplayerSynchronizer` and shared game code all just work, with zero protocol
reimplementation.

It needs the native WebRTC extension server-side (built and verified in this
fork already — `webrtc_web.md` §3) and a signaling server, which is a fraction
of the work below.

This is not an argument against the project — it is an argument about **where
the line sits**. The recommendation in §5 keeps the authoritative simulation in
headless Godot *and* builds the Node side, with Node owning the control plane
and the signaling. You get Node where Node wins and the engine where the engine
wins. Choose a Node *simulation* only when the gameplay logic genuinely wants
to be JS, or when per-box room density matters more than code sharing.

---

## 4. The previous attempt: `multiplayer-maze` (Unity + geckos.io, 2024)

*Read from `/mnt/109313D2109313D2/unity-game-dev/multiplayer-maze`: the server's
`index.js` (13.9 kB), the bridge `public/StreamingAssets/sys/js/main.js` (218
lines), the client `Assets/data/scripts/VuplexWebRTC.cs` (540 lines), and both
repos' git logs. Credit where due: it shipped, it had matchmaking, private
rooms, host migration, reconnection and time sync, and the commit messages are
unusually honest about what was being traded away.*

### 4.1 What was built

A Node + Express + geckos.io server (`cube-run-server`) that also served the
Unity WebGL build (brotli, `express-static-gzip`, CORS). It kept
`rooms[roomID] = { players[6], objects, settings }`, with matchmaking, a
waiting queue, private rooms by short hex code, host flagging and host
migration, JSON events (`room-new`, `room-join`, `player-sync`, `object-sync`,
`bulk`, `heartbeat`, `time-sync`, `game-over`...), and a 5 s heartbeat reaper.

Authority was **host-authoritative with a server-side mirror**: the host client
spawned objects and ran enemy AI, the server stored and relayed state, and
`isHost` migrated on disconnect.

The client could not talk to geckos directly — there is no C# geckos client — so
it ran the geckos **JavaScript** client inside a **Vuplex webview** and bridged
every message as a JSON string over `postMessage`, deserializing with
Newtonsoft on the C# side.

### 4.2 Lessons, and what each one means for this project

**L1 — Don't pick a transport whose only client is JavaScript.**
The Vuplex bridge is the defining feature of that architecture, and it exists
only because geckos has no engine-native client. Every message crossed
C# → JSON → `postMessage` → JS → geckos → JSON → WebRTC, and back. Two
serialization passes and an iframe hop per packet, plus a whole commercial
webview dependency in the shipping build, to send a position.

*Applies directly:* **Godot does not need this and must never do it.** The
engine speaks WebRTC natively on web *and* native (`modules/webrtc/`), so the
right move is to reimplement geckos' ~120-line handshake in GDScript
(§2.2) and talk to the data channel directly. This single difference is why the
Godot version of this idea is far more attractive than the Unity version was.
It is also the clearest argument for the "be a Godot peer, not a geckos peer"
recommendation: being a Godot peer means **zero** bridge code, not just a
thinner one.

**L2 — Separate the control plane from the data plane on day one.**
The server's git history is a round trip: WebSockets → `"This is a massive
breaking change, switching from websockets to WebRTC, may not be permanant"` →
`"adding first draft for dual websockets + webrtc server"` → `"added websockets
back"`. Room joins, matchmaking and lobby state are state-of-record: they want
reliable, ordered delivery and they are not latency-critical. Player positions
want the opposite. One unreliable channel for both forced a rediscovery of that
split the hard way.

*Applies directly:* the architecture in §1 puts auth/matchmaking/rooms on
HTTPS (plus WebSocket if push is needed) and only gameplay on the data
channels. Godot's channel 1 is there for in-match reliable traffic. **This is
settled before the first line of code, not after a rewrite.**

**L3 — Decide the binary format and the quantization up front.**
Everything was JSON — `JSON.stringify({[event]: data})` on the wire, Newtonsoft
on the client, and a full player struct (13 fields including names and booleans)
per sync. The consequences are visible in the log: `"testing lowered
precision"`, `"fixing quaternion w not being rounded"`, `"Removing the math
ops, clients do it now"`, `"testing a server synced players (instead of player
synced players) - hoping for some performance increases on slow connections"`.
Those are all the same bug — too many bytes per tick — being attacked from
four directions.

*Applies directly:* raw `PackedByteArray` over the data channel, with an
explicit per-field quantization table (position as 16-bit fixed point in a known
world range, angles as a byte or a 16-bit, booleans bit-packed into a flags
byte) decided and *written down* before the first snapshot ships. Godot's own
variant encoding via `@rpc` is already compact and handles this acceptably for
events; hand-pack only the high-rate snapshot path. And measure bytes/tick/player
in the test harness so a regression shows up as a number, not as a feeling.

**L4 — One tick for the process, never a timer per connection.**
`index.js` calls `setInterval(..., 14)` **inside** `gecko.onConnection`. So
every connected player gets their own ~71 Hz timer, and each one walks the whole
room and serializes every other player for that one recipient. Six players is
36 serializations per 14 ms, unsynchronized with each other and with the host's
simulation, and the cost is quadratic in room size. There is a second
`setInterval(..., 5000)` reaper and a client-side `setInterval(..., 32)`
batcher, so three independent clocks shape the traffic and none of them is the
simulation's.

*Applies directly:* **one fixed-rate authoritative tick per process**
(20-30 Hz is normal; 60 Hz only if the game needs it), building each room's
snapshot **once** and then fanning it out. Per-client work is limited to
delta/relevancy filtering against an already-built snapshot. The tick rate is
config, it is logged, and the harness asserts it.

**L5 — "Server-mirrored, host-authoritative" is the worst of both.**
The server stored room and player state but the host client owned it: the host
spawned objects, ran enemy AI, and `isHost` migrated on leave — with
`// TODO: figure out why the room closes when the host leaves` sitting in
`LeaveRoom`. So the deployment paid for a server and still got host-dependent
simulation, host-advantage latency, host-trivial cheating, and a migration
problem.

*Applies directly:* **if you are paying for a dedicated server, make it the
authority.** That is the whole justification for this project; without it, the
existing `WebRTCMultiplayerPeer` mesh is cheaper and about as good. And it is
the strongest argument for keeping the sim in headless Godot — authority is
easy there because the server runs the same code as the client.

**L6 — Lifecycle belongs in one place, and disconnect paths need tests.**
The previous stack ran an app-level heartbeat (client every 2.5 s, server reaps
at 5 s) *on top of* a transport that already reports connection state, plus a
1 Hz `setInterval` reconnect poll in the bridge. The seams show:
`heartbeats[channel.id]` is never deleted on disconnect (unbounded growth);
the reaper calls `channel.close()` and *then* `channel.emit('server-disconnect')`
on the closed channel; `LeaveAllRooms` loops over every room but calls
`LeaveRoom(channel, data)` without passing the loop variable, so it only ever
leaves the channel's current room; `playersWaitingForSlot` is never pruned of
disconnected channels, so matchmaking can hand a slot to a ghost.

*Applies directly:* derive liveness from the data channel's own state plus one
timeout, keep a single `channel → room` index so membership has exactly one
source of truth, and **test the disconnect paths explicitly**. This fork already
learned the same lesson in the same area: `webrtc_web.md`'s F2 (packets
destroyed on close) and the `mesh_close` fixture exist precisely because
close-path behavior is where this family of bugs lives.

**L7 — Don't let the realtime server serve the game assets.**
Express served the WebGL build with brotli and a hand-written `*.br`
content-type middleware. Convenient for a demo; in production it couples
client-asset deploys to realtime-server restarts, puts static-file I/O on the
latency-critical event loop, and makes CDN caching awkward. Static hosting is
free elsewhere. (It is also worth noting the fork's web exports need COOP/COEP
headers when `threads=yes` — another reason asset serving wants its own
properly-configured host.)

**L8 — No tests existed, in either repo.**
`"test": "echo \"Error: no test specified\" && exit 1"`. For a stack whose
failure modes are *timing-dependent, loss-dependent and browser-dependent*,
that is the lesson with the longest tail — every one of L1-L6 was discovered by
playing the game. This repo has already proved the alternative works:
`webgpu_tests/webrtc_parity/` found and fixed F1/F2 in days by diffing a shared
GDScript fixture across two targets. **The harness is Phase 1 here, not
Phase 6.**

### 4.3 What the previous attempt got right and should be kept

- **Short hex room codes + matchmaking + a waiting queue + private rooms.** The
  feature set is correct and the UX is proven; port the model, fix the indexing.
- **`userData` on the channel** as the per-connection identity bag — this maps
  straight onto geckos' auth hook and is where our Godot peer id goes (§2.2).
- **Client-side batching with per-key coalescing.** `main.js` accumulated
  updates for 32 ms and merged repeated updates for the same entity into one,
  keeping the newest values and OR-ing sticky flags (`dead`). Coalescing
  rather than queueing is the right instinct under unreliable transport, and
  it is a good fit for the snapshot design — note it also happens to be the
  correct mitigation for F3 (§6.2).
- **`time-sync`** (round-trip offset against the server clock) existed and was
  wired up before it was needed. Keep it; authoritative simulation needs a
  shared clock for input timestamping and interpolation delay.
- **Honest commit messages.** `"may not be permanant"`, `"hoping for some
  performance increases"`, the `TODO`s left in place. That history is why §4.2
  could be written at all. Keep doing it.

---

## 5. Integration architectures, compared

Four candidates. Where each one puts the protocol boundary is what separates
them.

### 5.1 Option A — speak geckos' own dialect from GDScript

A GDScript client that does the §2.2 handshake, opens the single
`geckos.io`-labeled channel, and implements `{event: payload}` JSON framing.

**Pros:** works against an unmodified geckos server; smallest server-side
effort; the geckos ecosystem (`snapshot-interpolation`) applies directly.
**Cons:** one unreliable unordered channel for everything; JSON framing;
"reliable" by 10× repetition (§2.4); and **no `@rpc`, no `MultiplayerAPI`, no
spawners or synchronizers** — every multiplayer feature is hand-built in
GDScript. It also fails the user's stated goal of patching into the normal
multiplayer systems.
**Verdict: rejected**, except as the Phase 0 spike (§7) where talking to a stock
geckos server is exactly what proves the handshake.

### 5.2 Option B — a `MultiplayerPeerExtension` wrapping a geckos connection

Keep geckos' transport but implement Godot's `MultiplayerPeerExtension`
(`_get_packet`, `_put_packet`, `_get_available_packet_count`,
`_get_unique_id`, …) over it, so `MultiplayerAPI` and `@rpc` work on top.

**Pros:** restores `@rpc` without changing the server's dialect; scriptable in
GDScript today, portable to GDExtension later.
**Cons:** you are emulating multi-channel reliability over a single unreliable
channel — i.e. reimplementing SCTP badly — or accepting that
`TRANSFER_MODE_RELIABLE` is a lie. Plus a new engine-adjacent component to
maintain.
**Verdict: rejected for the transport**, but **keep the technique in reserve**:
if a *future* requirement needs a transport Godot has no peer for (WebTransport,
say), this is the right shape, and it is also the fallback if Phase 0 finds a
blocker in `WebRTCMultiplayerPeer`.

### 5.3 Option C — **the recommendation**: a Node server that *is* a Godot WebRTC peer

Build the Node server on `node-datachannel` directly, and have it open the three
negotiated channels with ids 1/2/3 exactly as §2.6 describes, sending raw
payloads. Reuse geckos' HTTP signaling *shape* (§2.2), with the assigned Godot
peer id delivered in `userData`.

The Godot client is then **stock engine code**:

```gdscript
# ~150 lines of signaling glue, identical on web and native.
var res := await _post_json("%s/.wrtc/v2/connections" % url, auth_token)
var my_id: int = res.userData.peer_id          # server-assigned, >= 2

var conn := WebRTCPeerConnection.new()
conn.initialize({ "iceServers": res.userData.ice_servers })

var mp := WebRTCMultiplayerPeer.new()
mp.create_client(my_id)
mp.add_peer(conn, 1)                            # MUST precede set_remote_description:
                                                # add_peer requires STATE_NEW to create
                                                # the three negotiated channels.
conn.session_description_created.connect(func(type, sdp):
    conn.set_local_description(type, sdp)
    _post_json("%s/.wrtc/v2/connections/%s/remote-description" % [url, res.id],
               { "type": type, "sdp": sdp }))

conn.set_remote_description("offer", res.localDescription.sdp)
# then poll GET /connections/{id}/additional-candidates on the §2.2 backoff
# and feed each into conn.add_ice_candidate(...)

multiplayer.multiplayer_peer = mp               # @rpc now works
```

**Pros:**
- **No new client-side transport code, on either target.** `@rpc`,
  `MultiplayerAPI`, `MultiplayerSpawner`, `MultiplayerSynchronizer` all work
  because the peer *is* `WebRTCMultiplayerPeer`.
- Real per-channel reliability, from SCTP, with no emulation.
- Raw payloads — no JSON framing tax, and the snapshot path can be hand-packed
  bytes.
- The same GDScript runs in a native build, so the parity harness pattern that
  already works in this repo (`webgpu_tests/webrtc_parity/compare.sh`) applies
  unchanged.
- Server and desktop client sit on the same libdatachannel (§2.1).

**Cons:**
- The server must honor Godot's channel convention exactly — but that
  convention is three lines of config (§2.6), and it is now *documented and
  verified*, which is most of the risk retired.
- We maintain the signaling server (small) instead of using geckos' (small).
- It is coupled to an engine-internal convention: if upstream changes channel
  ids or adds a handshake, the server must follow. Mitigation: a parity fixture
  that asserts the convention, so an engine bump fails the test rather than the
  game. (The §2.7 bug is a live example of why reading the convention is not
  enough — it must be *tested*.)

**Verdict: adopt.**

### 5.4 Option D — Option C plus the authoritative simulation in Node

Everything in C, and the Node process also *originates* gameplay traffic as
peer 1 — server-authored `@rpc` calls, server-driven spawns and synchronizer
state.

This is where the cost jumps, because originating traffic means implementing
Godot's application protocol in JavaScript:

- **`SceneMultiplayer`'s command byte** — `NETWORK_COMMAND_REMOTE_CALL`,
  `SIMPLIFY_PATH`, `CONFIRM_PATH`, `RAW`, `SPAWN`, `DESPAWN`, `SYNC`, `SYS`
  (`modules/multiplayer/scene_multiplayer.h:68-80`).
- **The RPC meta byte and its variable-width ids** — node-path-cache id and rpc
  method id each encoded in 1, 2 or 4 bytes depending on magnitude, plus a
  `byte_only_or_no_args` fast path (`scene_rpc_interface.cpp:43-47, 349-410`).
- **The node path cache handshake** — `SIMPLIFY_PATH`/`CONFIRM_PATH`, including
  the rule that a path is only usable once the receiver has confirmed it.
- **Godot's variant binary encoding** (`core/io/marshalls.cpp`) for every type
  the game's RPCs use, plus `MultiplayerAPI::encode_and_compress_variants`'
  packing.
- **`SYS_COMMAND_AUTH`** if `SceneMultiplayer`'s auth callback is used.
- And for replication: `MultiplayerSynchronizer`'s property ordering, which is
  derived from scene-side configuration and would have to be mirrored in JS and
  kept in sync with the scenes by hand.

All tractable — none of it is novel — but it is a 2-3 week build, it needs its
own conformance test suite against a real Godot peer, and it is a **permanent
tax**: every engine upgrade can move any of it, and the failure mode is a
malformed packet in production rather than a compile error.

**Verdict: deliberately deferred.** Do Option C, keep authority in headless
Godot (§3.3), and revisit D only with a concrete reason — JS gameplay logic,
or room density per box that headless Godot can't match. If D ever happens,
**Tier 1 relay (§7 Phase 3) is the stepping stone**: the server can relay
opaque packets without understanding a single byte of them, which is enough for
room/lobby topologies and for proving the transport under real load before
anyone writes a variant encoder in JavaScript.

---

## 6. Constraints inherited from this fork's WebRTC work

These come from `webgpu_notes/webrtc_web.md` (measured, same day as this doc).
They are design inputs, not risks to discover later.

### 6.1 Parity is established — the transport is not the risk

17 of 18 channel observables match between web and native, and the
`WebRTCMultiplayerPeer` layer adds no divergence of its own beyond F5
(unset reliability getters: `0` on web, `-1` on native — cosmetic for us, since
our channels set these explicitly). `threads=yes` is byte-identical to
`nothreads`. **So a client built on Option C behaves the same in a browser and
in a native build**, which is what makes one GDScript implementation and one
test fixture viable.

### 6.2 F3 is the dominant design constraint: a 64 KB silently tail-dropping receive buffer

Web receives into a fixed `RingBuffer` sized by
`network/limits/webrtc/max_channel_in_buffer_kb` (**default 64 KB**) and
**drops** when full; native has an unbounded queue and never drops. Measured:
**500 × 1000-byte packets sent in one tick → 65 delivered, 435 dropped**, with
`put_packet()` returning `OK` every time and *no* API-level signal on either
side. It is a clean tail-drop, so the receiver cannot even detect it by looking
for sequence gaps — the stream just stops. The engine prints one console error
per dropped packet (435 of them in that run), which at volume is its own
problem.

Design consequences, all of which belong in the plan rather than in a
post-mortem:

1. **Raise `max_channel_in_buffer_kb`** in the client project settings, and
   document the value alongside the chosen snapshot size and tick rate. Treat
   `tick_rate × snapshot_bytes × worst-case frame hitch` as a budget the buffer
   must cover.
2. **Drain every channel to empty every frame**, unconditionally. Any frame that
   polls once and stops is a potential drop.
3. **Coalesce rather than queue on the send side**, which is what §4.3's
   32 ms batcher got right: one snapshot per entity per tick, newest wins.
4. **Keep snapshots small** (L3) — the buffer is a byte budget, so quantization
   directly buys headroom.
5. **Add a drop detector to the harness.** A sequence number in the snapshot
   header makes the tail-drop visible as a number; without one it is invisible
   by construction.
6. Note that the §2.7 `maxPacketLifetime` bug makes this *worse* — retransmitted
   stale snapshots consume the same scarce buffer as fresh ones.

### 6.3 Other inherited facts worth carrying

- **Max packet size:** `get_max_packet_size()` now returns 16384 (was 1200,
  fixed as F1), but **measured delivery works up to 65530 bytes** in Chrome;
  65531 and above are dropped by the receive buffer, not by the send path. Keep
  individual packets well under a path MTU anyway — SCTP will fragment, and
  fragmentation plus an unreliable channel is a bad combination.
- **Final packets at disconnect:** F2 is fixed, so a closed channel's buffered
  packets are now drainable on web as on native — *but* reading them through
  `WebRTCMultiplayerPeer` is impossible on **every** platform, because
  `poll()` calls `remove_peer()` the moment a channel leaves `STATE_OPEN`.
  If the design needs a client's last input before it drops, it must be
  acknowledged in-protocol; do not expect the transport to deliver it.
- **Fixtures must be `_process`-driven, never blocking.** A `while` loop with
  `OS.delay_msec()` works natively and *can never work* on a single-threaded
  web build — no callback ever fires. Every test fixture here follows
  `webrtc_parity`'s `extends Node` + `_process()` form.
- **webrtc-native aborts (exit 134) on a project's first filesystem scan** (F6).
  Any CI step that runs the native half must do a throwaway `--import` first
  and ignore its exit code.
- **`dlink_enabled=yes` is untested for WebRTC.** If exported games use
  GDExtensions, that combination needs its own check.

---

## 7. Implementation plan

Effort assumes one developer who knows this codebase. Each phase states its
exit criterion; **a phase is not done until its test runs green from a clean
checkout.** Tiering follows §5.4: Phases 0-3 give a working relay-topology
dedicated server; Phase 5 chooses where authority lives.

### Phase 0 — handshake spike against a stock geckos server (1-2 days)

Prove the riskiest unknown before building anything: **can GDScript complete
the §2.2 handshake, on web and native, with no JavaScript?**

- Stand up an unmodified `@geckos.io/server@3.1.0` echo server (`multiplex:
  true`, CORS on, `Authorization` allowed) in
  `webgpu_tests/geckos_backend/reference_server/`.
- Write `geckos_handshake.gd` as a `webrtc_parity`-style fixture (`extends
  Node`, `_process`-driven, `RTC|`-prefixed output lines) doing: POST
  `/connections` → `WebRTCPeerConnection.initialize()` →
  `set_remote_description("offer", sdp)` → POST the answer → poll
  `additional-candidates` on the documented backoff → wait for the
  `geckos.io`-labeled channel → echo a payload both ways.
- Run it natively and in Chromium via the existing Playwright runner, and diff.

**Exit:** a byte round-trip on both targets, plus written answers to G1-G5 (§9),
especially **G3** (does peer-reflexive discovery actually carry the connection
with a candidate-free answer?) and **G1** (does `add_peer()`-before-
`set_remote_description()` produce a usable answer for negotiated channels?).

*If G1 fails, Option C is in trouble and Option B becomes the fallback — which
is exactly why this phase comes first and is cheap.*

### Phase 1 — Node transport core, `node-datachannel` direct (3-5 days)

`server/` in a new repo or under `webgpu_tests/geckos_backend/server/`:

- `node-datachannel` peer connections, **single UDP port** (`enableIceUdpMux`),
  configurable `portRange`, ICE/TURN config from env.
- **Godot channel convention (§2.6):** three negotiated channels, ids 1/2/3,
  `ordered` true/true/false, and `maxPacketLifeTime` spelled **correctly** on
  2 and 3 (§2.7).
- Signaling HTTP server in the §2.2 shape, with `userData` carrying
  `{ peer_id, ice_servers, tick_rate }`; **plus a client-candidate endpoint**
  (`POST /connections/{id}/ice-candidate`) that geckos lacks, so TURN-only
  clients are possible — and a trickle path for Godot's `ice_candidate_created`.
- Peer-id allocation (server is 1, clients ≥ 2, ids never reused inside a
  match), connection lifecycle from channel state plus **one** timeout (L6),
  and a single `connection → room` index.
- **One fixed-rate tick for the process** (L4), rate from config, logged.

**Exit:** `node run_tests.mjs` in a new `webgpu_tests/geckos_backend/`
tier — a Node-side harness using `node-datachannel/polyfill` as a fake client —
covering connect, three-channel open with correct ids/ordering, raw echo,
disconnect cleanup (no leaked ids, rooms, or timers), and peer-id reuse rules.

### Phase 2 — the Godot client addon (2-3 days)

`addons/godot_dedicated_client/` — pure GDScript, no JS, no GDExtension:

- `DedicatedClient` node: `connect_to_server(url, token)`, signals
  `connected(peer_id)` / `connection_failed(reason)` / `disconnected(reason)`,
  reconnect with backoff, and **lifecycle in one place** (L6).
- Hands back a configured `WebRTCMultiplayerPeer` for
  `multiplayer.multiplayer_peer`, so `@rpc` works with no further ceremony.
- Sets and documents `max_channel_in_buffer_kb`; drains every channel to empty
  every frame (§6.2); exposes a drop/sequence-gap counter.
- `time_sync`: round-trip server clock offset (L4/§4.3), exposed as a property.

**Exit:** `compare.sh`-style parity run of one fixture on native and web with
**identical** normalized output, plus a `@rpc` round-trip through the Node
relay on both targets.

### Phase 3 — Tier 1: opaque relay, rooms, and the harness that keeps it honest (2-3 days)

The server routes packets between clients **without parsing them** — it is
peer 1 on the wire but understands no payload.

- Rooms: create/join by short code, private rooms, matchmaking queue, capacity —
  port the previous attempt's model (§4.3) with a single membership index (L6).
- Fan-out: targeted, broadcast, broadcast-except-sender, mapped onto Godot's
  `target_peer` convention including the negative "exclude" form
  (`webrtc_multiplayer_peer.cpp:400-414`).
- Instrumentation: bytes/tick/player, packets dropped, buffer high-water,
  per-room tick jitter. **L3 and §6.2 are only enforceable as numbers.**

**Exit:** a scripted N-client load test (headless Godot clients plus polyfill
clients) holding the configured tick rate with zero drops, and a documented
bytes/tick/player figure.

### Phase 4 — the orchestrator (4-6 days)

The part that justifies Node:

- Auth: short-lived signed tokens consumed by the `Authorization` header on
  `POST /connections`; the token, not the client, decides the peer id.
- Matchmaking and room registry across multiple game-server processes.
- Game-server supervision: spawn/health-check/drain/reap, port allocation,
  graceful shutdown with player notification.
- Operational surface: `/healthz`, metrics, structured logs, config by env.
- Static assets served **elsewhere** (L7).

**Exit:** two game-server processes, one orchestrator, clients matchmade across
both; killing a game server drains and reports rather than hanging clients.

### Phase 5 — authority (choose one)

**5a — headless Godot authoritative (recommended, 2-3 days).** Export a headless
server build with the native WebRTC extension (already built and verified,
`webrtc_web.md` §3), register it with the orchestrator, let it be peer 1 for its
room. Full `@rpc`/spawner/synchronizer support, shared game code, no protocol
reimplementation. Remember F6's throwaway-`--import` step in CI.

**5b — Node authoritative (10-15 days, deferred).** Implement §5.4's protocol
list in JS, with a conformance suite diffing every packet against a real Godot
peer. Only on a concrete requirement.

### Phase 6 — hardening (4-5 days)

- TURN (coturn) over UDP and **TCP/443**, and a measurement of the latency cost
  for clients forced onto it.
- Browser matrix: Chrome, Firefox, Safari (this fork's Firefox blockers are
  fixed — Tasks 38/41 — so Firefox is in scope from the start).
- `threads=yes` **and** `dlink_enabled=yes` exports (§6.3).
- Loss/latency injection (`tc netem`) with F3 drop counters asserted.
- Reconnect, mid-match server death, room cleanup under churn.
- `local_ci.sh` integration for the parts that can run headless.

**Totals:** Phases 0-4 + 5a + 6 ≈ **17-24 working days (3-4 weeks)**.
With 5b instead of 5a ≈ **6-7 weeks**.

### Suggested layout

```
webgpu_tests/geckos_backend/
  README.md                  # tier docs, in this repo's house style
  reference_server/           # stock geckos.io, Phase 0 only
  server/                     # the real Node server (node-datachannel)
    src/{signaling,transport,rooms,tick,orchestrator}/
  client_addon/               # addons/godot_dedicated_client/ (GDScript)
  fixtures/dedicated_project/ # one project, exports to web and runs native
  run_tests.mjs               # Node-side transport tests
  compare.sh                  # native vs web parity, webrtc_parity pattern
```

---

## 8. Risks, and what would kill it

| risk | severity | mitigation / kill criterion |
|---|---|---|
| **G1:** `add_peer()` before `set_remote_description()` doesn't yield a usable answer for negotiated channels | **high** | Phase 0 answers it in a day. If it fails: Option B (§5.2), or renegotiate ordering with a server-side `datachannel`-event design. |
| **G3:** candidate-free answer + peer-reflexive discovery doesn't connect in practice | high | Phase 0; mitigated by the extra client-candidate endpoint already in Phase 1. |
| F3 tail-drop sinks the snapshot design (§6.2) | **high** | Buffer sizing, drain-to-empty, coalescing, quantization, sequence counters — all Phase 1-3 deliverables, not afterthoughts. |
| §2.7 lifetime bug makes "unreliable" channels reliable | medium | One-char upstream fix; server-side spelled correctly from day one; measure it (proposed F8). |
| Upstream changes the channel convention | medium | A parity fixture asserting ids/labels/ordering, so an engine bump fails a test. |
| TURN needed by more clients than expected | medium | Measure the TURN-fallback rate in Phase 6; budget coturn. |
| `node-datachannel` native-module friction (prebuilds, libc, Node ABI) | low-medium | Pin the version, build in a container, pin the Node major. Alive and actively released (§2.1). |
| geckos.io goes unmaintained | **low** | It is a reference protocol here, not a dependency — this is one of the main reasons for Option C. |
| Node single-core sim limits room density | low (5a) / medium (5b) | Horizontal scale via the orchestrator; it is the orchestrator's purpose. |

**Kill criteria — reasons to stop and reconsider:**

1. Phase 0 shows GDScript cannot drive the handshake without JavaScript. The
   value proposition (one client implementation, no bridge — L1) collapses and
   this becomes a much less attractive project.
2. The game design turns out not to need server authority (§3.1). Then
   `WebRTCMultiplayerPeer` mesh plus ~100 lines of signaling is the right answer
   and this whole stack is over-engineering.
3. F3 cannot be made to hold at the required tick rate even after §6.2's
   mitigations. Then the snapshot design needs rethinking (lower rate,
   delta compression, interest management) *before* any server work continues.

---

## 9. Open questions for the Phase 0 spike

Everything inferred rather than verified, gathered so none of it leaks into the
design unexamined.

**G1 — Ordering of `add_peer()` vs `set_remote_description()`.** `add_peer()`
requires `STATE_NEW` to create its channels
(`webrtc_multiplayer_peer.cpp:299`), so it must run first; does the engine then
produce an answer that correctly advertises the three negotiated channels, given
the remote offer was authored by a server that created its own? *Highest-risk
unknown in the document.*

**G2 — Negotiated channels against a non-Godot peer.** Both sides create ids
1/2/3 independently. Verified as the engine's intent by reading the source;
never tested against a non-Godot peer.

**G3 — Candidate-free answers.** Does the server reliably learn the client's
address peer-reflexively (§2.2)? What breaks for a TURN-only client?

**G4 — `HTTPRequest` on web.** CORS preflight with an `Authorization` header,
behavior under HTTPS, and whether the polling schedule is affordable on the main
thread of a single-threaded build.

**G5 — Godot's ICE trickle.** `ice_candidate_created` fires after the answer is
sent. Can those candidates be delivered late and still help (hence the Phase 1
endpoint), and does the engine accept remote candidates added after the
connection is already established?

**G6 — Real snapshot budget.** With `max_channel_in_buffer_kb` raised to X, what
tick rate and snapshot size survive a 250 ms frame hitch without an F3 drop?
This is the number the whole data-plane design hangs on, and it is measurable in
Phase 1.

**G7 — `maxPacketLifeTime` behavior** once spelled correctly: does Chrome
actually abandon a packet at lifetime 1 ms, and what does Firefox/Safari do?
(Relates to the proposed F8 in §2.7.)

**G8 — Headless Godot server density.** How many rooms/players does one headless
Godot process carry versus one Node process? This is the number that decides
whether Option D (§5.4) ever needs to happen.

---

## 10. Recommendation in one paragraph

Build it, but build the *right* thing: a Node orchestrator that owns auth,
matchmaking, rooms, signaling and game-server lifecycle, plus a transport layer
on `node-datachannel` that presents itself to Godot as a stock WebRTC peer —
three negotiated channels, ids 1/2/3, raw payloads — so the Godot side is
`WebRTCMultiplayerPeer` plus ~150 lines of GDScript signaling and `@rpc` keeps
working on web and native from one implementation. Take geckos.io's HTTP
signaling shape and its architecture as proven prior art, and leave its JSON
framing and its send-it-ten-times reliability behind, because Godot already
does both of those things properly and adopting them would cost the entire
engine multiplayer stack. Keep authoritative simulation in headless Godot until
there is a concrete reason to reimplement Godot's variant encoding in
JavaScript. Start with the one-to-two-day Phase 0 handshake spike against a
stock geckos server, because a single question — G1 — decides whether the
recommended architecture works at all, and it is cheap to answer. And carry
forward the previous attempt's hardest-won lesson: write the harness first, in
the same shape as `webgpu_tests/webrtc_parity/`, because every one of L1-L6 was
a bug that a test would have found before a player did.

---

## 11. How much better off would a Node-side simulation make you?

*Added 2026-10-12, in answer to a direct question about §5.4 / Option D. This
section supersedes §5.4's effort estimate (revised **up**, from 10-15 days to
13-20) and converts the rest of that section's hand-waving into measured
numbers.*

**Short answer: at the scale this is realistically being built for, barely at
all — and the one benefit that is real (density) is worth less money per year
than the build costs in a week.** The crossover is at roughly **tens of
thousands of concurrent players**, and below that the expected value of Option D
is negative. But the question has a better answer than "no": **split authority
by domain rather than by layer** (§11.6) and you capture most of Node's value
for about a tenth of Option D's cost.

### 11.1 Measured baselines (this machine, 2026-10-12)

Everything below is reproducible; method in §12.2. Host: 24 cores, 61 GB RAM,
Node v22.22.0, this fork's `bin/godot.linuxbsd.template_release.x86_64`.

| | headless Godot | Node |
|---|---|---|
| what was measured | exported **dedicated-server release template**, empty scene, 30 Hz physics | `http.createServer` + a 30 Hz `setInterval` |
| time to application-ready | **175-201 ms** (5 runs) | **1 ms** |
| process peak RSS | **~105 MB** (105.0-106.0 MB, 5 runs) | **~43-53 MB** |
| deployed artifact | 94 MB binary + pck | a few hundred KB of JS + a native addon |

Marginal cost per room in Node, from a proxy holding 16 players × 64 bytes of
float state plus per-player bookkeeping, all driven from **one** process-wide
30 Hz tick (L4):

| rooms | RSS | marginal |
|---|---|---|
| 0 | 42.8 MB | — |
| 100 | 50.6 MB | ~78 KB/room |
| 1 000 | 59.8 MB | **~17 KB/room** |
| 5 000 | 105.1 MB | **~12.5 KB/room** |

The headline comparison, and it is a fair one only as an order-of-magnitude
anchor: **5 000 rooms in one Node process cost about the same RSS as one empty
headless Godot process.** The structural reason is that Godot's unit of
isolation is a process (realistically, one per match) while Node's is an object.

Two honest corrections to that anchor, both of which narrow it:

- **An empty Godot scene is not a game.** A real match scene — meshes,
  collision shapes, Box3D bodies, scripts, navigation — will add tens to a few
  hundred MB. Budget **150-300 MB per match process** for a real 3D game, not
  105 MB.
- **A real Node sim room is not 13 KB.** My proxy stores state and increments
  it; it does not collide, navigate, or validate. Multiply by 10-50×
  (~150-650 KB/room) and Node is still RAM-cheap by orders of magnitude — at
  which point **CPU, not RAM, becomes Node's limit**, because it is one core per
  process.

### 11.2 The density argument, costed

Using the corrected figures and a 32 GB box with ~30 GB usable:

| | headless Godot (one process per match) | Node (many rooms per process) |
|---|---|---|
| binding constraint | **RAM** | **CPU** (single-threaded JS) |
| per unit | 150-300 MB per match | ~50 µs/room/tick ⇒ ~600 rooms per core at 30 Hz *(estimate, unmeasured)* |
| matches per box | **~100-200** | **~500-5 000** (24 cores, several processes) |
| at 8 players/match | ~800-1 600 CCU | ~4 000-40 000 CCU |

So Option D buys roughly **10-25× density**. Now price it. At ~$40/month for
that box:

| sustained CCU | Godot-path hosting | Node-path hosting | **annual saving** |
|---|---|---|---|
| 100 | 1 box, $40/mo | 1 box, $40/mo | **$0** |
| 1 000 | 1 box, $40/mo | 1 box, $40/mo | **$0** |
| 5 000 | 3-6 boxes, $120-240/mo | 1 box, $40/mo | **~$1-2k** |
| 50 000 | 30-60 boxes, $1.2-2.4k/mo | 2-12 boxes, $80-480/mo | **~$13-28k** |

*(Estimates. They ignore bandwidth, which is identical on both paths and at
scale usually dominates compute anyway — a point that cuts further against
Option D.)*

**Below ~1 000 CCU the density advantage is worth literally nothing**, because
both paths fit on the smallest box you would rent anyway. Option D's build cost
is 13-20 working days *plus* a permanent tax (§11.4). At 5 000 sustained CCU it
pays back in roughly a decade. At 50 000 it pays back in about a month — so if
the honest plan is a game with tens of thousands of concurrent players, this
section's conclusion flips, and §11.7 says exactly when to revisit.

### 11.3 The arguments for Option D, graded against measurement

| claim | grade | measured reality |
|---|---|---|
| "Node is far denser" | ✅ **true**, 10-25× | §11.2 — but worth ~$0/yr below ~1 000 CCU |
| "Room spawn is instant; Godot cold-start hurts matchmaking" | ❌ **dead** | Godot reaches `_ready` in **180 ms**. Pre-warm a couple of processes and spawn latency is invisible. This argument does not survive contact with the measurement. |
| "One language for orchestrator + sim" | 🟡 half-true | True *within the server*. But it makes the **server↔client** split bilingual, which is the worse seam (§11.4). |
| "Better ops: observability, hot reload, containers" | ✅ true, modest | Real but small: ~94 MB images and no hot reload versus `npm ci`, pino/OTel, and `nodemon`. Days of convenience, not weeks of value. |
| "JS ecosystem for the sim" | 🟡 weak | Godot *is* a game-sim ecosystem. The JS gain is in non-sim concerns, which Node already owns on the recommended path. |
| "The developer is stronger in JS" | ✅ real, and the best argument here | `multiplayer-maze` shipped a working Node server (§4). Velocity is a legitimate input — but it argues for Node owning the **control plane**, which §11.6 gives you anyway without the protocol tax. |

### 11.4 The costs, itemized

**1. The protocol tax — 13-20 days (revised up from §5.4's 10-15).** To
*originate* gameplay traffic, Node must reimplement Godot's application protocol
in JavaScript, bit-exactly:

| piece | source of truth | days |
|---|---|---|
| Variant binary encode/decode (bool, ints with the 64-bit flag, floats, String, Vector2/2i/3/3i, Rect2, Transform2D/3D, Quaternion, Plane, Basis, AABB, Color, Packed*Array, Array, Dictionary) including 4-byte alignment padding | `core/io/marshalls.cpp` | 3-4 |
| `encode_and_compress_variants` and its byte-only fast path | `scene/main/multiplayer_api.cpp` | 1-2 |
| RPC meta byte + the 1/2/4-byte variable-width node-cache and method ids | `scene_rpc_interface.cpp:43-47, 349-410` | 2-3 |
| Node path cache handshake: `SIMPLIFY_PATH`/`CONFIRM_PATH`, and the rule that a cached id is unusable until confirmed | `scene_multiplayer.cpp` | 2-3 |
| `SYS_COMMAND_AUTH`, peer add/remove ordering | `scene_multiplayer.h:68-80` | 1 |
| `SPAWN`/`DESPAWN`/`SYNC` — synchronizer property ordering mirrored from scene-side replication config, spawn custom data | `multiplayer_spawner.cpp`, `multiplayer_synchronizer.cpp` | 4-6 |
| Conformance suite diffing every packet against a real Godot peer | — | 2-3 |

None of it is research; all of it is exacting. And the `SYNC` row is the one
that bites: synchronizer property order is **derived from scene configuration**,
so a designer reordering properties in the editor silently changes the wire
format, and nothing in the JS server knows. That is a permanent footgun pointed
at content authors, not just at engineers.

**2. A permanent compatibility tax.** Every piece above is engine-internal and
unversioned. A Godot minor can move any of it, the failure mode is a malformed
packet in production rather than a build error, and this fork *already* tracks
upstream releases (`sync/*` branches, Phase 8/15). Budget **1-3 days per engine
upgrade, forever**, plus the risk that one upgrade lands on the `SYNC` encoding
and costs a week. §2.7 is the cautionary example: a wire-affecting bug that sat
in upstream unnoticed because *reading* the convention is not the same as
testing it.

**3. Gameplay logic written twice — the real cost.** Authoritative movement must
agree with client prediction, or reconciliation jitters and rubber-bands.
With the sim in GDScript on both ends, the authoritative code *is* the predicted
code. With the sim in JS, every movement rule, collision response, cooldown and
edge case exists in two languages and must be kept in lockstep by hand, forever.
This is the hardest bug class in multiplayer development, and §4.2's L5 is the
previous attempt walking into the adjacent version of it — two places thought
they owned the truth, and the `TODO: figure out why the room closes when the
host leaves` is what that feels like from the inside.

**4. No reuse of the engine's own systems** — Box3D physics (now this fork's
default 3D engine), navigation, `CharacterBody3D` movement, animation, the scene
tree. All of it would be reimplemented or approximated.

*One genuine mitigation, worth recording because this repo is unusually well
placed to use it:* `thirdparty/box3d` is **plain C17**, and this fork already
has a configured Emscripten toolchain. Compiling Box3D to WASM and loading it
in Node gives the server *the same physics implementation* as the Godot client,
which collapses cost #4 from "reimplement physics" to "bind an existing
library". Caveat: cross-build float determinism (FMA, fast-math, libm
differences) means the two builds may diverge in the last bits — acceptable,
because the server is authoritative and reconciliation only needs the client's
prediction to be *close*, not identical. If Option D ever happens, **this is the
trick that makes it tractable**, and it should be prototyped before anything
else in that path.

### 11.5 Net verdict

| scenario | better off with a Node sim? |
|---|---|
| ≤1 000 CCU, matches of 2-16, 3D with physics and client prediction | **No — clearly worse.** Pay 3-4 weeks plus a permanent tax to save $0/yr and duplicate your gameplay code. |
| 1 000-5 000 CCU, moderate sim | **No, marginally.** ~$1-2k/yr saved; the build alone outweighs it. Revisit if growth is steep. |
| ≥10 000 CCU with a *simple* sim (2D/arcade, 50+ players per room, no deep physics) | **Yes.** Density dominates, and a simple sim is cheap to express twice. This is geckos.io's home turf and where `snapshot-interpolation` earns its keep. |
| Deep 3D physics sim at any scale | **No.** Duplication cost is unbounded and prediction divergence is the worst bug class you can buy. |
| Metagame-heavy, realtime-light (persistence, economy, progression, social) | **Wrong question** — that work belongs in Node regardless, and §11.6 is how you get it. |

### 11.6 The better answer: split authority by domain, not by layer

The question "Node sim or Godot sim?" assumes one authority. Don't have one.
Split on what is worth cheating for versus what is worth sharing code for:

| domain | authority | why |
|---|---|---|
| accounts, sessions, entitlements | **Node** | security boundary; never in a game client |
| matchmaking, rooms, invites, presence | **Node** | §4.3's model, already proven in your own prior work |
| inventory, currency, progression, unlocks, leaderboards | **Node** | the things actually worth cheating for; wants a database and transactions, not a scene tree |
| match simulation: movement, collision, hit registration, scoring | **headless Godot** | shares code with the client's prediction; reuses Box3D, navigation, animation |
| match results, anti-cheat verdicts | **Node validates what Godot reports** | the game server is a trusted-but-verified reporter, and Node holds the ledger |

This is the standard industry split, it is what the §1 architecture already
draws, and it gets you: Node where Node wins, no protocol reimplementation, no
duplicated gameplay code, one language per domain rather than two per feature.
**Cost: the 2-3 day Phase 5a glue (§7) instead of 13-20 days plus a permanent
tax** — and if scale later forces Option D, Phase 3's opaque relay and this
same split are exactly the foundation it needs.

### 11.7 Triggers to revisit

Revisit Option D only when a measured threshold trips, not on a hunch:

1. **Sustained CCU > 5 000** with hosting cost a visible fraction of revenue.
2. **A Godot match process exceeds ~300 MB** or cannot hold its tick with the
   target player count — measure before believing it (G8, §9).
3. **The sim turns out to be simple** — no deep physics, no prediction, ≥50
   players per room.
4. **The Box3D-to-WASM spike (§11.4) succeeds**, which removes the largest
   single cost item.
5. **The team becomes JS-majority** and GDScript becomes the bottleneck.

Until then: the measured answer to "how much better off?" is **"not better off
at all, and ~3-4 weeks worse."**

---

## 12. Appendices

### 12.1 Full implementation schedule

The week-by-week plan, with dependencies, decision gates, parallel tracks and
exit criteria, is in **`webgpu_notes/geckos_backend_schedule.md`**. §7 above is
its summary; the schedule doc is the version to work from.

### 12.2 Reproducing §11.1's measurements

Headless Godot (empty project, `physics_ticks_per_second=30`, a `main.gd` that
prints `Time.get_ticks_msec()` in `_ready()` and quits):

```bash
# The release template refuses --path/--main-pack (disable_path_overrides is on
# by default for templates), so the project must be *exported* to be measured.
# export_presets.cfg: platform="Linux", dedicated_server=true,
#   custom_template/release=<bin/godot.linuxbsd.template_release.x86_64>,
#   binary_format/embed_pck=true
bin/godot.linuxbsd.editor.x86_64 --headless --path <proj> \
    --export-release "Linux" <out>/bench.x86_64
/usr/bin/time -f "%e s peakRSS=%M KB" <out>/bench.x86_64 --headless
```

Node baseline and the per-room proxy: `node_base.mjs` (an `http` server plus a
30 Hz `setInterval`) and `node_rooms.mjs` (`ROOMS` rooms × 16 players ×
`Float32Array(16)`, all advanced from one process-wide 30 Hz tick), both run
under `/usr/bin/time -f "%e s peakRSS=%M KB"`. Both scripts are ~15 lines; they
were written in a scratchpad and are trivially reconstructed from the
descriptions above, or from §11.1's table if the numbers need re-verifying.

**Caveats, stated so these numbers are not over-read:** single machine, warm
page cache, empty Godot scene, trivial Node rooms, no network, no clients
attached, no TLS. They bound the *floor* of each approach and support an
order-of-magnitude comparison — they are not capacity planning. Real capacity
is G8 in §9, and it is a Phase 1/3 measurement.
