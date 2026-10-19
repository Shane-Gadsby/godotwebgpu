# F2 at the WebRTCMultiplayerPeer layer: packets that arrived but were not
# drained when the remote peer goes away.
#
# `mesh.gd` cannot show this — it drains every tick, so no backlog ever exists.
# Here the receiver deliberately leaves packets queued, then the sender closes.
#
# Two reads, because the layer has two places a packet can die:
#   mp_*  — through WebRTCMultiplayerPeer. Its poll() calls remove_peer() as
#           soon as a channel leaves STATE_OPEN, which drops the peer's channels
#           outright, so a loss here is structural and platform-independent.
#   ch_*  — through a Ref to the reliable channel taken while connected, which
#           outlives remove_peer(). This is the F2 question proper: does the
#           channel object still hold its undrained packets after close?
extends "res://tests/rtc_test_base.gd"

var mp1: WebRTCMultiplayerPeer
var mp2: WebRTCMultiplayerPeer
var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection

# Kept alive across remove_peer() on purpose; see the header.
var ch2_reliable: WebRTCDataChannel

var seen_connected := {}
var disconnected := false
var close_tick := -1
var queued_before_close := -1

const PACKETS := 3
const PAYLOAD := "f2-mp-pending"

func _ready() -> void:
	phase = "connecting"
	mp1 = WebRTCMultiplayerPeer.new()
	mp2 = WebRTCMultiplayerPeer.new()
	out("mesh1=%d mesh2=%d" % [mp1.create_mesh(1), mp2.create_mesh(2)])

	pc1 = WebRTCPeerConnection.new()
	pc2 = WebRTCPeerConnection.new()
	out("impl=%s" % pc1.get_class())
	pc1.initialize({})
	pc2.initialize({})
	pc1.session_description_created.connect(_on_sdp.bind(1))
	pc2.session_description_created.connect(_on_sdp.bind(2))
	pc1.ice_candidate_created.connect(_on_ice.bind(1))
	pc2.ice_candidate_created.connect(_on_ice.bind(2))

	mp1.peer_connected.connect(func(id): seen_connected[1] = id)
	mp2.peer_connected.connect(func(id): seen_connected[2] = id)
	mp2.peer_disconnected.connect(func(id): disconnected = true; out("mp2 peer_disconnected=%d" % id))

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

func _step() -> void:
	match phase:
		"connecting":
			# Gate on peer_connected, never get_connection_status(): a mesh
			# reports CONNECTED the moment create_mesh() returns.
			if seen_connected.has(1) and seen_connected.has(2):
				out("MESH CONNECTED after %d ticks" % ticks)
				var info: Dictionary = mp2.get_peer(1)
				var chans: Array = info.get("channels", [])
				out("mp2 channels=%d" % chans.size())
				if chans.size() > 0:
					ch2_reliable = chans[0]
				mp1.set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_RELIABLE)
				mp1.set_target_peer(2)
				for i in range(PACKETS):
					out("send i=%d err=%d" % [i, mp1.put_packet(PAYLOAD.to_utf8_buffer())])
				phase = "wait_queue"

		"wait_queue":
			# Let them arrive, then close WITHOUT draining a single one.
			if mp2.get_available_packet_count() >= PACKETS:
				queued_before_close = mp2.get_available_packet_count()
				out("mp_queued_before_close=%d ch_queued_before_close=%d" % [
					queued_before_close,
					ch2_reliable.get_available_packet_count() if ch2_reliable else -1])
				mp1.close()
				close_tick = ticks
				phase = "wait_close"
			elif ticks > 1200:
				out("packets never queued (avail=%d)" % mp2.get_available_packet_count())
				phase = "done"

		"wait_close":
			if disconnected:
				out("disconnect observed after %d ticks" % (ticks - close_tick))
				_verdict()
			elif ticks - close_tick > 400:
				out("disconnect NOT observed in 400 ticks")
				_verdict()

func _verdict() -> void:
	var mp_avail := mp2.get_available_packet_count()
	var mp_got := mp2.get_packet() if mp_avail > 0 else PackedByteArray()
	out("mp_avail_after_disconnect=%d mp_drain_len=%d verdict=%s" % [
		mp_avail, mp_got.size(), "DRAINED" if mp_got.size() > 0 else "LOST"])

	var ch_avail := ch2_reliable.get_available_packet_count() if ch2_reliable else -1
	var ch_state := ch2_reliable.get_ready_state() if ch2_reliable else -1
	var ch_got := PackedByteArray()
	if ch2_reliable and ch_avail > 0:
		ch_got = ch2_reliable.get_packet()
	out("ch_state=%d ch_avail_after_disconnect=%d ch_drain_len=%d content='%s' verdict=%s" % [
		ch_state, ch_avail, ch_got.size(), ch_got.get_string_from_utf8(),
		"DRAINED" if ch_got.size() > 0 else "LOST"])
	phase = "done"

func _process(_delta: float) -> void:
	if phase == "done":
		return
	ticks += 1
	mp1.poll()
	mp2.poll()
	_step()
	if ticks >= 2000 and phase != "done":
		out("TIMEOUT in phase=%s" % phase)
		phase = "done"
	if phase == "done":
		finish()
