# Dedicated-Server Backend — Full Implementation Schedule

**Scope:** the week-by-week build plan for the Node dedicated-server backend
and its Godot client interfaces, as scoped in
`webgpu_notes/geckos_dedicated_server.md` (read that first — this document
assumes its §2 protocol facts, §5 architecture choice, §6 constraints and §11
authority split without restating them).
**Date:** 2026-10-12
**Branch at time of writing:** `webgpu-4.8` (`cc9fd1cc62`)
**Status:** **not started.** Nothing in this schedule has been built.

**Architecture being built** (evaluation doc §5.3 + §11.6): a Node orchestrator
and transport built on `node-datachannel`, presenting itself to Godot as a stock
WebRTC peer (three negotiated channels, ids 1/2/3, raw payloads), with
authoritative match simulation in headless Godot and authoritative metagame in
Node. The Godot client is stock `WebRTCMultiplayerPeer` plus a GDScript
signaling addon, so `@rpc` works unchanged on web and native.

---

## 0. How to read this schedule

- **Day counts are working days for one developer who knows this codebase.**
  Ranges are honest, not padded: the low end assumes nothing surprises you, the
  high end assumes one thing per phase does.
- **"Exit" is a gate, not a wish.** A phase is done when its stated test runs
  green from a clean checkout. No phase is "done except the tests".
- **Decision gates (G-gates)** are points where a measurement decides the next
  phase. They are the only places where the plan is allowed to change shape.
- **Every phase ships its tests with it.** This is lesson L8 of the previous
  attempt (evaluation doc §4.2) and the reason `webgpu_tests/webrtc_parity/`
  found F1/F2 in days. The harness is not a later phase.
- **Nothing here requires pushing anything.** All work lands as local commits on
  a topic branch off `webgpu-4.8`.

---

## 1. Timeline at a glance

Recommended path (headless-Godot authority), **17-24 working days ≈ 3.5-5
calendar weeks**:

```
week 1   P0 handshake spike ██          G1/G3 gate
         P1 transport core     ████████
week 2   P1 (cont.)          ██
         P2 Godot client addon  ██████
week 3   P3 relay + rooms + harness ██████
         P4 orchestrator              ████
week 4   P4 (cont.)          ████████
         P5a headless Godot authority    ██████
week 5   P6 hardening        ██████████
```

| phase | days | depends on | gate at exit |
|---|---|---|---|
| **P0** handshake spike | 1-2 | — | **G1, G3** — decides whether the architecture works |
| **P1** Node transport core | 3-5 | P0 | — |
| **P2** Godot client addon | 2-3 | P1 (can start late in P1) | — |
| **P3** relay, rooms, instrumentation | 2-3 | P1, P2 | **G6** — snapshot budget vs F3 |
| **P4** orchestrator | 4-6 | P1 (independent of P2/P3) | — |
| **P5a** headless Godot authority | 2-3 | P3, P4 | **G8** — density, informs §11.7 |
| **P6** hardening | 4-5 | all | ship gate |
| *P5b* Node authority *(deferred)* | *13-20* | *P3* | *only after §11.7 triggers* |

**Parallelism:** P4 (orchestrator) touches no WebRTC code and can run alongside
P2/P3 if a second developer is available — that is the only genuinely
independent track, and it compresses the calendar to ~3 weeks. Everything else
is a chain.

---

## 2. Phase 0 — handshake spike (1-2 days)

**Purpose:** answer the one question that can invalidate the whole
architecture, before spending money on it. Everything else in this schedule is
ordinary engineering; this is the research.

**Risk being retired:** G1 — `WebRTCMultiplayerPeer.add_peer()` requires the
connection in `STATE_NEW` to create its three negotiated channels
(`webrtc_multiplayer_peer.cpp:299`), so it must run *before*
`set_remote_description()`. Does the engine then produce an answer that works
against a server-authored offer? Nobody has tested Godot's negotiated channels
against a non-Godot peer.

### Tasks

1. `webgpu_tests/geckos_backend/reference_server/` — an unmodified
   `@geckos.io/server@3.1.0` echo server: `multiplex: true`, CORS on,
   `Authorization` allowed, one UDP port, ICE config from env. Pinned
   `package.json`, no build step.
2. `fixtures/dedicated_project/tests/geckos_handshake.gd` — a
   `webrtc_parity`-shaped fixture (`extends Node`, `_process`-driven,
   `RTC|`-prefixed output; **never** a blocking `while` + `OS.delay_msec()`,
   which cannot work on a single-threaded web build — evaluation doc §6.3).
   It performs: `POST /connections` → `WebRTCPeerConnection.initialize()` →
   `add_peer()` → `set_remote_description("offer", sdp)` → POST the answer →
   poll `additional-candidates` on the documented backoff → wait for channel
   open → echo a payload each way.
3. Run it natively and in Chromium through the existing Playwright runner
   (borrow Playwright from `webgpu_tests/screenshot_comparison/node_modules`,
   as `webrtc_parity` does), and diff the normalized output.
4. Write the answers to **G1-G5** into the evaluation doc §9, replacing the
   *inferred* labels with *verified* ones.

### Exit criteria

- A byte round-trip against a stock geckos server on **both** targets.
- G1 answered with evidence (SDP dumps kept).
- G3 answered: does the candidate-free answer connect via peer-reflexive
  discovery, and what happens to a TURN-only client?
- G4 answered: `HTTPRequest` CORS preflight with `Authorization` under HTTPS.

### If the gate fails

- **G1 fails** (no usable answer for negotiated channels): fall back to
  evaluation doc §5.2 — a `MultiplayerPeerExtension` over a single channel —
  and re-cost the project. This changes the plan's shape, which is exactly why
  it costs 2 days to find out rather than 2 weeks.
- **G3 fails**: not fatal; P1 already ships the client-candidate endpoint
  geckos lacks. It does mean non-trickle clients are unsupported, and TURN
  becomes mandatory sooner.

---

## 3. Phase 1 — Node transport core (3-5 days)

**Deliverable:** a Node process that any stock Godot client can connect to as
peer 1, built on `node-datachannel` directly (not on geckos — evaluation doc
§5.1).

### Tasks

1. **Peer connections.** `node-datachannel`, pinned version, single UDP port
   (`enableIceUdpMux`), configurable `portRange`, ICE/TURN config from env,
   TLS-terminated signaling.
2. **Godot channel convention** (evaluation doc §2.6) — the specification this
   phase exists to meet:
   | id | label | config |
   |---|---|---|
   | 1 | `reliable` | `{ negotiated: true, id: 1, ordered: true }` |
   | 2 | `ordered` | `{ negotiated: true, id: 2, ordered: true, maxPacketLifeTime: N }` |
   | 3 | `unreliable` | `{ negotiated: true, id: 3, ordered: false, maxPacketLifeTime: N }` |
   Payloads are **raw bytes** — no framing, no JSON, no length prefix.
   Note `maxPacketLifeTime` with the **correct** spelling: Godot writes
   `maxPacketLifetime` and nothing reads it (evaluation doc §2.7), so the server
   must not copy the engine's typo.
3. **Signaling**, in the geckos shape (evaluation doc §2.2): `POST
   /connections`, `POST /connections/{id}/remote-description`, `GET
   /connections/{id}/additional-candidates`, `POST /connections/{id}/close`,
   **plus `POST /connections/{id}/ice-candidate`** — the client-trickle endpoint
   geckos does not have, needed for Godot's `ice_candidate_created` and for
   TURN-only clients.
4. **`userData`** carries `{ peer_id, ice_servers, tick_rate, protocol_version }`.
   The server allocates peer ids (server = 1, clients ≥ 2, never reused inside a
   match). `protocol_version` exists so a mismatched client fails loudly at
   connect rather than mysteriously mid-match.
5. **Lifecycle in one place** (L6): liveness from data-channel state plus **one**
   timeout. No app-level heartbeat, no second reconnect clock, no per-connection
   timers. A single `connection → room` index is the only membership truth.
6. **One fixed-rate process tick** (L4), rate from config and logged at startup.
   Per-room snapshots are built **once** and then fanned out; per-client work is
   filtering only.

### Exit criteria

`cd webgpu_tests/geckos_backend && node run_tests.mjs` green, using
`node-datachannel/polyfill` as a synthetic client, covering:

- connect; three channels open with the right ids, labels and ordering;
- raw echo on each channel, including a >16 KB payload;
- disconnect cleanup: no leaked peer ids, rooms, timers or candidate lists
  (assert on counts, not on absence of crashes — L6's leaks were all silent);
- peer-id allocation rules, including refusal to reuse an in-match id;
- `protocol_version` mismatch rejected at `POST /connections`.

---

## 4. Phase 2 — the Godot client addon (2-3 days)

**Deliverable:** `addons/godot_dedicated_client/` — pure GDScript, no
JavaScript, no GDExtension, identical on web and native.

### Tasks

1. **`DedicatedClient` node.** `connect_to_server(url, token)`; signals
   `connected(peer_id)`, `connection_failed(reason)`, `disconnected(reason)`;
   reconnect with backoff; all lifecycle in this one class (L6).
2. **Hands back a configured `WebRTCMultiplayerPeer`** for
   `multiplayer.multiplayer_peer`, with the ordering that G1 validated:
   `create_client(peer_id)` → `add_peer(conn, 1)` → `set_remote_description()`.
   After that, `@rpc` is live and the addon is out of the way.
3. **F3 defenses** (evaluation doc §6.2), as code and as documentation:
   - set and document `network/limits/webrtc/max_channel_in_buffer_kb`;
   - **drain every channel to empty every frame**, unconditionally;
   - a sequence counter per channel with a gap/tail-drop detector, exposed as a
     property and printed in the fixtures. Without it, F3 is invisible by
     construction.
4. **`time_sync`** — round-trip server clock offset (§4.3 of the evaluation
   doc), exposed as a property, because authoritative simulation needs a shared
   clock for input timestamping and interpolation delay.
5. **Send-side coalescing helper**: newest-wins per entity per tick, the thing
   `multiplayer-maze`'s 32 ms batcher got right and the correct F3 mitigation.

### Exit criteria

- `./compare.sh dedicated_connect` produces **identical** normalized output
  native vs web (the `webrtc_parity` pattern: any surviving diff line is a
  divergence).
- A `@rpc` round-trip through the Node relay on both targets.
- The drop detector reads zero across a 60-second idle-plus-traffic run, and
  correctly reports non-zero when the buffer is deliberately undersized —
  **test the detector, not just the happy path.**

---

## 5. Phase 3 — Tier 1 relay, rooms, instrumentation (2-3 days)

**Deliverable:** a working dedicated server for relay topologies. The server is
peer 1 on the wire and **parses no payload** — which is what makes this phase
cheap and what makes it the stepping stone to P5b if it is ever needed.

### Tasks

1. **Rooms**: create/join by short hex code, private rooms, matchmaking queue,
   capacity limits. Port `multiplayer-maze`'s model (evaluation doc §4.3) with a
   single membership index and the §4.2 L6 bugs fixed by construction —
   specifically: prune disconnected channels from the waiting queue, delete
   per-connection bookkeeping on disconnect, and never iterate rooms while
   mutating membership.
2. **Fan-out** mapped onto Godot's `target_peer` convention, including the
   negative "exclude this peer" form (`webrtc_multiplayer_peer.cpp:400-414`).
3. **Instrumentation** — L3 and F3 are only enforceable as numbers:
   bytes/tick/player, packets dropped, receive-buffer high-water, per-room tick
   jitter, channel `bufferedAmount`. Exported for Prometheus and printed by the
   harness.

### Exit criteria + **gate G6**

- An N-client load test (headless Godot clients plus polyfill clients) holds the
  configured tick rate with **zero** drops, and the run prints a documented
  bytes/tick/player figure.
- **G6 answered:** with `max_channel_in_buffer_kb = X`, what tick rate and
  snapshot size survive a 250 ms client frame hitch without an F3 drop? Write
  the answer into the evaluation doc §6.2. **This number constrains the entire
  data-plane design** — if it comes out badly, stop and redesign the snapshot
  (lower rate, deltas, interest management) before continuing to P4.

---

## 6. Phase 4 — the orchestrator (4-6 days)

**Deliverable:** the part that justifies choosing Node at all (evaluation doc
§11.6). Independent of P2/P3 and parallelizable.

### Tasks

1. **Auth**: short-lived signed tokens, consumed via the `Authorization` header
   on `POST /connections`. **The token decides the peer id, never the client.**
2. **Matchmaking and room registry** across multiple game-server processes —
   the thing geckos' in-process rooms cannot do (evaluation doc §2.5).
3. **Game-server supervision**: spawn, health-check, drain, reap; port
   allocation; graceful shutdown that notifies players rather than dropping
   them. Pre-warm a small pool — headless Godot reaches `_ready` in **~180 ms**
   (evaluation doc §11.1), so spawn latency is a non-issue once warm.
4. **Metagame authority** (§11.6): inventory, currency, progression,
   leaderboards, match-result validation. Node holds the ledger; the game server
   is a trusted-but-verified reporter.
5. **Operations**: `/healthz`, structured logs, metrics, config by env, and a
   container image. **Static game assets are served elsewhere** (L7) — note the
   fork's `threads=yes` exports also need COOP/COEP headers, another reason
   asset hosting wants its own properly-configured host.

### Exit criteria

- Two game-server processes plus one orchestrator; clients matchmade across
  both.
- Killing a game server mid-match **drains and reports** rather than hanging
  clients; the room disappears from the registry; players get a reason.
- A token cannot be replayed, and a forged peer id is rejected.

---

## 7. Phase 5a — headless Godot authority (2-3 days, recommended)

**Deliverable:** authoritative match simulation, sharing code with the client.

### Tasks

1. Export a **headless dedicated-server build** (`dedicated_server=true`) with
   the native WebRTC extension — already built and verified in this fork
   (`webrtc_web.md` §3).
2. Register with the orchestrator on boot; accept its room assignment; be peer 1
   for that room via `WebRTCMultiplayerPeer.create_server()`.
3. Authoritative movement/collision with client prediction and reconciliation,
   reusing Box3D and the client's own movement code — the whole point of this
   path (evaluation doc §11.4, cost #3).
4. **CI note:** webrtc-native aborts with **exit 134** on a project's first
   filesystem scan (F6), so any CI step must run a throwaway `--import` first
   and ignore its exit code.

### Exit criteria + **gate G8**

- A full match runs end to end: matchmake → connect → play → result reported to
  and validated by the orchestrator → room torn down.
- Server-authoritative movement with client prediction, and a measured
  reconciliation error budget.
- **G8 answered:** RSS and sustained tick cost per match process at the target
  player count. Compare against the §11.1 baseline (105 MB empty) and the
  §11.7 trigger (~300 MB). **This is the number that decides whether P5b ever
  happens.** Write it into the evaluation doc §9.

---

## 8. Phase 6 — hardening (4-5 days)

### Tasks

1. **TURN** (coturn) over UDP **and TCP/443**, plus a measurement of the
   latency penalty and the *fraction of clients* forced onto it. That fraction
   is a hosting line item.
2. **Browser matrix**: Chrome, Firefox, Safari. Firefox is in scope from the
   start — this fork's Firefox blockers are fixed (Tasks 38, 41).
3. **Export matrix**: `threads=yes` (verified byte-identical for WebRTC) **and
   `dlink_enabled=yes`**, which is untested for WebRTC (evaluation doc §6.3) and
   matters if exported games use GDExtensions.
4. **Loss and latency injection** (`tc netem`): 1%/5%/10% loss, 50/150/300 ms
   RTT, with F3 drop counters and reconciliation error asserted, not eyeballed.
5. **Churn and failure**: reconnect mid-match, server death, orchestrator
   restart, room cleanup under rapid join/leave. These are the paths L6's bugs
   lived in.
6. **`local_ci.sh` integration** for everything that runs headless.

### Exit criteria

A documented, reproducible green run of the whole matrix, plus a one-page
operational runbook (deploy, scale, roll back, read the metrics).

---

## 9. Deferred: Phase 5b — Node-side authority (13-20 days)

**Do not start this without a §11.7 trigger.** Evaluation doc §11 costs it in
full: 13-20 days to build, 1-3 days per engine upgrade forever, gameplay logic
written twice, and the `SYNC` encoding is derived from scene configuration so a
designer reordering properties silently changes the wire format.

If it ever happens, the order is fixed by risk:

1. **Spike the Box3D-to-WASM trick first** (evaluation doc §11.4). `thirdparty/box3d`
   is plain C17 and this fork has a configured Emscripten toolchain, so the
   server can run the *same* physics implementation as the client. This removes
   the single largest cost item, and if it fails, 5b should not proceed.
2. Variant encode/decode + conformance suite against a real Godot peer.
3. RPC meta byte, variable-width ids, node path cache handshake.
4. `SYS_COMMAND_AUTH`, peer add/remove ordering.
5. `SPAWN`/`DESPAWN`/`SYNC` last — the most fragile, and the easiest to defer
   indefinitely by keeping replication client-driven.

P3's opaque relay is the foundation for all of it: the transport is already
proven under load before anyone writes a variant encoder in JavaScript.

---

## 10. Repository layout

```
webgpu_tests/geckos_backend/
  README.md                     # tier docs, house style (cf. webrtc_parity/README.md)
  reference_server/             # stock geckos.io@3.1.0 — Phase 0 only
  server/                       # the real server (node-datachannel)
    src/signaling/              # §2.2-shaped HTTP + the client-candidate endpoint
    src/transport/              # Godot channel convention, peer ids, lifecycle
    src/rooms/                  # membership, matchmaking, fan-out
    src/tick/                   # the one process tick
    src/orchestrator/           # auth, registry, supervision, metagame
  client_addon/                 # → addons/godot_dedicated_client/ (GDScript)
  fixtures/dedicated_project/   # one project: exports to web, runs native
    tests/                      # _process-driven fixtures, RTC| output lines
  run_tests.mjs                 # Node-side transport tests
  compare.sh                    # native vs web parity (webrtc_parity pattern)
```

---

## 11. Risk register (schedule view)

The full register is evaluation doc §8. Here it is as schedule impact:

| risk | phase | if it fires |
|---|---|---|
| G1: negotiated channels vs a non-Godot peer | **P0** | architecture changes shape → fall back to `MultiplayerPeerExtension`, re-cost |
| G6: F3 budget unworkable | **P3** | stop; redesign the snapshot before P4 |
| G3: candidate-free answers don't connect | P0/P1 | TURN becomes mandatory; +1-2 days |
| §2.7 lifetime bug | P1 | server spells it correctly; upstream fix is one character |
| Upstream moves the channel convention | P6 / future syncs | parity fixture fails the build instead of the game |
| `node-datachannel` native-module friction | P1 | pin version + Node major, build in a container |
| G8: Godot process too heavy | P5a | §11.7 trigger → consider P5b (+13-20 days) |

---

## 12. Before any of this starts

Three cheap things, each independently useful, that should land first because
they are all in the critical path's blast radius:

1. **Fix the `maxPacketLifetime` typo** (evaluation doc §2.7) — one character in
   `modules/webrtc/webrtc_multiplayer_peer.cpp`, and upstreamable. Currently
   `TRANSFER_MODE_UNRELIABLE_ORDERED` and `TRANSFER_MODE_UNRELIABLE` are both
   silently reliable on every platform, which would quietly poison every
   snapshot-rate measurement in P3 and P6.
2. **Measure it as F8** in `webgpu_tests/webrtc_parity/` — send on channel 3
   across a lossy link and check for retransmission. Reading the convention was
   not enough to catch this; a test would have been.
3. **Decide the CCU target honestly.** Evaluation doc §11 shows the entire
   Node-vs-Godot authority question collapses to this one number, and the answer
   changes a 3-week plan into a 7-week one. It is cheaper to decide it now than
   at P5.
