# MultiplayerAPI / @rpc over WebRTCMultiplayerPeer — the layer projects actually
# write against, above the MultiplayerPeer contract every other fixture covers.
#
# Two SceneMultiplayer instances in one process, each with its own
# WebRTCMultiplayerPeer, attached to two sibling subtrees via
# SceneTree.set_multiplayer(api, root_path). Each subtree holds a `Pong` node at
# the same relative path, which is how an RPC resolves on the far side.
#
#   PeerA (multiplayer root, api A, mesh id 1)       PeerB (root, api B, id 2)
#   └── Pong  <-- rpc_pong.gd                        └── Pong
#
# Covers: @rpc in both directions, get_remote_sender_id(), a broadcast with no
# explicit target, and all three transfer modes — which matters more since F8,
# because the unreliable modes now really are unreliable (channels 2 and 3 carry
# a 1 ms packet lifetime). On a lossless loopback they must still all arrive.
extends "res://tests/rtc_test_base.gd"

var mp1: WebRTCMultiplayerPeer
var mp2: WebRTCMultiplayerPeer
var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection
var api1: SceneMultiplayer
var api2: SceneMultiplayer
var pong1: Node
var pong2: Node

var seen_connected := {}
var pings := []   # [from, payload, mode] as received by B
var pongs := []   # ... by A
var step_idx := 0
var sent_tick := -1

const MODES := ["reliable", "unreliable_ordered", "unreliable"]

func _ready() -> void:
	phase = "connecting"
	mp1 = WebRTCMultiplayerPeer.new()
	mp2 = WebRTCMultiplayerPeer.new()
	out("mesh1=%d mesh2=%d" % [mp1.create_mesh(1), mp2.create_mesh(2)])

	# Two multiplayer roots, each with an identically-named child.
	var root_a := Node.new()
	root_a.name = "PeerA"
	add_child(root_a)
	var root_b := Node.new()
	root_b.name = "PeerB"
	add_child(root_b)

	pong1 = load("res://tests/rpc_pong.gd").new()
	pong1.name = "Pong"
	root_a.add_child(pong1)
	pong2 = load("res://tests/rpc_pong.gd").new()
	pong2.name = "Pong"
	root_b.add_child(pong2)

	api1 = SceneMultiplayer.new()
	api2 = SceneMultiplayer.new()
	api1.multiplayer_peer = mp1
	api2.multiplayer_peer = mp2
	get_tree().set_multiplayer(api1, root_a.get_path())
	get_tree().set_multiplayer(api2, root_b.get_path())
	out("api1=%s api2=%s" % [api1.get_class(), api2.get_class()])
	out("roots a=%s b=%s" % [str(root_a.get_path()) != "", str(root_b.get_path()) != ""])
	out("unique_id api1=%d api2=%d" % [api1.get_unique_id(), api2.get_unique_id()])

	pong2.got_ping.connect(func(from, payload, mode): pings.append([from, payload, mode]))
	pong1.got_pong.connect(func(from, payload): pongs.append([from, payload]))

	api1.peer_connected.connect(func(id): seen_connected[1] = id)
	api2.peer_connected.connect(func(id): seen_connected[2] = id)

	pc1 = WebRTCPeerConnection.new()
	pc2 = WebRTCPeerConnection.new()
	out("impl=%s" % pc1.get_class())
	pc1.initialize({})
	pc2.initialize({})
	pc1.session_description_created.connect(_on_sdp.bind(1))
	pc2.session_description_created.connect(_on_sdp.bind(2))
	pc1.ice_candidate_created.connect(_on_ice.bind(1))
	pc2.ice_candidate_created.connect(_on_ice.bind(2))
	out("add_peer mp1=%d mp2=%d" % [mp1.add_peer(pc1, 2), mp2.add_peer(pc2, 1)])
	pc1.create_offer()

func _on_sdp(type: String, sdp: String, who: int) -> void:
	out("sdp from=%d type=%s len=%d" % [who, type, sdp.length()])
	if who == 1:
		pc1.set_local_description(type, sdp)
		pc2.set_remote_description(type, sdp)
	else:
		pc2.set_local_description(type, sdp)
		pc1.set_remote_description(type, sdp)

func _on_ice(media: String, index: int, name: String, who: int) -> void:
	if who == 1:
		pc2.add_ice_candidate(media, index, name)
	else:
		pc1.add_ice_candidate(media, index, name)

func _send_step() -> void:
	match step_idx:
		0:
			out("rpc_id(2, ping) reliable")
			pong1.rpc_id(2, "ping", "hello-reliable")
		1:
			out("rpc_id(2, ping_unreliable_ordered)")
			pong1.rpc_id(2, "ping_unreliable_ordered", "hello-uo")
		2:
			out("rpc_id(2, ping_unreliable)")
			pong1.rpc_id(2, "ping_unreliable", "hello-u")
		3:
			# No explicit target: broadcast to every connected peer.
			out("rpc(ping) broadcast")
			pong1.rpc("ping", "hello-broadcast")
		4:
			out("rpc_id(1, pong) from B back to A")
			pong2.rpc_id(1, "pong", "hello-back")
	sent_tick = ticks

func _step() -> void:
	match phase:
		"connecting":
			if seen_connected.has(1) and seen_connected.has(2):
				out("RPC MESH CONNECTED after %d ticks" % ticks)
				out("peers api1=%s api2=%s" % [str(api1.get_peers()), str(api2.get_peers())])
				phase = "rpc"
				_send_step()

		"rpc":
			# NB min() returns Variant, so an inferred `:=` here is a parse
			# error under this project's warnings-as-errors.
			var want_pings: int = min(step_idx + 1, 4)
			var want_pongs: int = 1 if step_idx >= 4 else 0
			if pings.size() >= want_pings and pongs.size() >= want_pongs:
				if step_idx < 4:
					var p = pings[pings.size() - 1]
					out("recv ping from=%d payload='%s' mode=%s" % [p[0], p[1], p[2]])
				else:
					var p = pongs[0]
					out("recv pong from=%d payload='%s'" % [p[0], p[1]])
					_verdict()
					return
				step_idx += 1
				_send_step()
			elif ticks - sent_tick > 300:
				out("rpc step=%d TIMEOUT pings=%d pongs=%d" % [step_idx, pings.size(), pongs.size()])
				_verdict()

func _verdict() -> void:
	# Sender ids must be the *other* peer's id on each side, and every payload
	# must arrive intact: an RPC that resolves to the wrong node or loses its
	# arguments is the failure mode worth catching here.
	var ping_ok := pings.size() == 4
	for p in pings:
		if p[0] != 1 or not String(p[1]).begins_with("hello-"):
			ping_ok = false
	var modes_seen := []
	for p in pings:
		if not modes_seen.has(p[2]):
			modes_seen.append(p[2])
	var pong_ok: bool = pongs.size() == 1 and pongs[0][0] == 2 and pongs[0][1] == "hello-back"
	out("rpc pings=%d sender_ids_ok=%s modes=%d" % [pings.size(), str(ping_ok), modes_seen.size()])
	out("rpc pongs=%d reverse_ok=%s" % [pongs.size(), str(pong_ok)])
	out("rpc verdict=%s" % ("OK" if ping_ok and pong_ok and modes_seen.size() == 3 else "BROKEN"))
	phase = "done"

func _process(_delta: float) -> void:
	if phase == "done":
		return
	ticks += 1
	# SceneTree polls registered MultiplayerAPIs itself, which is what drives
	# the peers; nothing here polls them by hand.
	_step()
	if ticks >= 2500 and phase != "done":
		out("TIMEOUT phase=%s step=%d" % [phase, step_idx])
		phase = "done"
	if phase == "done":
		finish()
