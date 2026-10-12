# WebRTCMultiplayerPeer parity: a two-peer mesh in one process, SDP/ICE
# shuttled directly. Exercises the layer real projects use — peer_connected /
# peer_disconnected signals, all three transfer modes, and the three channels
# the multiplayer peer builds per remote peer.
extends "res://tests/rtc_test_base.gd"

var mp1: WebRTCMultiplayerPeer
var mp2: WebRTCMultiplayerPeer
var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection

var seen_connected := {}
var seen_disconnected := {}
var mode_idx := 0
var sent_tick := -1
var close_tick := -1

const MODES := [
	["RELIABLE", MultiplayerPeer.TRANSFER_MODE_RELIABLE],
	["UNRELIABLE_ORDERED", MultiplayerPeer.TRANSFER_MODE_UNRELIABLE_ORDERED],
	["UNRELIABLE", MultiplayerPeer.TRANSFER_MODE_UNRELIABLE],
]

func _ready() -> void:
	phase = "connecting"
	mp1 = WebRTCMultiplayerPeer.new()
	mp2 = WebRTCMultiplayerPeer.new()
	out("mesh1=%d mesh2=%d" % [mp1.create_mesh(1), mp2.create_mesh(2)])
	out("unique_id mp1=%d mp2=%d" % [mp1.get_unique_id(), mp2.get_unique_id()])

	pc1 = WebRTCPeerConnection.new()
	pc2 = WebRTCPeerConnection.new()
	out("impl=%s" % pc1.get_class())
	pc1.initialize({})
	pc2.initialize({})
	pc1.session_description_created.connect(_on_sdp.bind(1))
	pc2.session_description_created.connect(_on_sdp.bind(2))
	pc1.ice_candidate_created.connect(_on_ice.bind(1))
	pc2.ice_candidate_created.connect(_on_ice.bind(2))

	mp1.peer_connected.connect(func(id): seen_connected[1] = id; out("mp1 peer_connected=%d" % id))
	mp2.peer_connected.connect(func(id): seen_connected[2] = id; out("mp2 peer_connected=%d" % id))
	mp1.peer_disconnected.connect(func(id): seen_disconnected[1] = id; out("mp1 peer_disconnected=%d" % id))
	mp2.peer_disconnected.connect(func(id): seen_disconnected[2] = id; out("mp2 peer_disconnected=%d" % id))

	# Both sides add the peer (the channels are negotiated with fixed ids), then
	# exactly one side offers.
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

func _dump_channels(mp: WebRTCMultiplayerPeer, tag: String, remote: int) -> void:
	var info: Dictionary = mp.get_peer(remote)
	if info.is_empty():
		out("%s get_peer(%d)=EMPTY" % [tag, remote])
		return
	var chans = info.get("channels", [])
	out("%s peer=%d connected=%s channels=%d" % [tag, remote, str(info.get("connected", false)), chans.size()])
	for i in range(chans.size()):
		var ch: WebRTCDataChannel = chans[i]
		out("%s ch%d ready=%d ordered=%s id=%d lifetime=%d rtx=%d negotiated=%s label=%s" % [
			tag, i, ch.get_ready_state(), str(ch.is_ordered()), ch.get_id(),
			ch.get_max_packet_life_time(), ch.get_max_retransmits(),
			str(ch.is_negotiated()), ch.get_label()])

func _step() -> void:
	match phase:
		"connecting":
			# NB: for a mesh, get_connection_status() is CONNECTED from the
			# moment create_mesh() succeeds — it says nothing about whether any
			# remote peer's channels are open. Gate on peer_connected instead,
			# or the first send races the channels and fails "DataChannel not open".
			if seen_connected.has(1) and seen_connected.has(2):
				out("MESH CONNECTED after %d ticks" % ticks)
				out("status mp1=%d mp2=%d" % [mp1.get_connection_status(), mp2.get_connection_status()])
				out("peers mp1=%s mp2=%s" % [str(mp1.get_peers()), str(mp2.get_peers())])
				_dump_channels(mp1, "mp1", 2)
				_dump_channels(mp2, "mp2", 1)
				phase = "send"

		"send":
			if mode_idx >= MODES.size():
				out("all modes done; closing mp1")
				mp1.close()
				close_tick = ticks
				phase = "disconnect"
				return
			var m = MODES[mode_idx]
			mp1.set_transfer_mode(m[1])
			mp1.set_target_peer(2)
			var payload := "mode-%s" % m[0]
			var err := mp1.put_packet(payload.to_utf8_buffer())
			out("send mode=%s err=%d" % [m[0], err])
			sent_tick = ticks
			phase = "recv"

		"recv":
			var m = MODES[mode_idx]
			if mp2.get_available_packet_count() > 0:
				var from := mp2.get_packet_peer()
				var p := mp2.get_packet()
				var got := p.get_string_from_utf8()
				out("recv mode=%s from=%d payload='%s' ok=%s" % [
					m[0], from, got, str(got == "mode-%s" % m[0] and from == 1)])
				mode_idx += 1
				phase = "send"
			elif ticks - sent_tick > 200:
				out("recv mode=%s TIMEOUT (no packet in 200 ticks)" % m[0])
				mode_idx += 1
				phase = "send"

		"disconnect":
			if seen_disconnected.has(2):
				out("disconnect observed after %d ticks" % (ticks - close_tick))
				out("mp2 status=%d peers=%s" % [mp2.get_connection_status(), str(mp2.get_peers())])
				_verdict()
			elif ticks - close_tick > 400:
				out("disconnect NOT observed in 400 ticks; mp2 status=%d peers=%s" % [
					mp2.get_connection_status(), str(mp2.get_peers())])
				_verdict()

func _verdict() -> void:
	out("verdict connected_both=%s disconnect_seen=%s" % [
		str(seen_connected.has(1) and seen_connected.has(2)),
		str(seen_disconnected.has(2))])
	phase = "done"

func _process(_delta: float) -> void:
	if phase == "done":
		return
	ticks += 1
	mp1.poll()
	mp2.poll()
	_step()
	if ticks >= 2000 and phase != "done":
		out("TIMEOUT in phase=%s mp1=%d mp2=%d" % [
			phase, mp1.get_connection_status(), mp2.get_connection_status()])
		phase = "done"
	if phase == "done":
		finish()
