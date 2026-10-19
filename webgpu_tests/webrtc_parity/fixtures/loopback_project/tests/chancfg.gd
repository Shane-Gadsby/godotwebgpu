# Does a channel's reliability configuration actually reach the transport?
#
# Four channels, each with one config, each read back through the public
# accessors on both ends. Covers the configs the plan listed for Phase 1 and
# the loopback fixture never exercised (it only ever used {"ordered": true}),
# and in particular settles whether the key `WebRTCMultiplayerPeer` writes is
# the key either backend reads:
#
#   typo    {"maxPacketLifetime": 1}  <- what webrtc_multiplayer_peer.cpp writes
#   correct {"maxPacketLifeTime": 1}  <- what both backends read, and what
#                                        RTCDataChannelInit defines
#
# If "typo" reads back unset while "correct" reads back 1, the key is silently
# dropped and TRANSFER_MODE_UNRELIABLE_ORDERED / TRANSFER_MODE_UNRELIABLE are
# fully reliable on every platform. Unset prints differently per platform
# (native -1, web 0) — that is F5, not this.
extends "res://tests/rtc_test_base.gd"

var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection

# label -> config. One reliability parameter each: libdatachannel rejects a
# config carrying both maxPacketLifeTime and maxRetransmits.
const CONFIGS := [
	["baseline", { "ordered": true }],
	["typo", { "ordered": true, "maxPacketLifetime": 1 }],
	["correct", { "ordered": true, "maxPacketLifeTime": 1 }],
	["rtx0", { "ordered": false, "maxRetransmits": 0 }],
]

var local := {}
var remote := {}

func _ready() -> void:
	phase = "connecting"
	pc1 = WebRTCPeerConnection.new()
	pc2 = WebRTCPeerConnection.new()
	out("impl=%s" % pc1.get_class())
	pc1.initialize({})
	pc2.initialize({})
	pc1.session_description_created.connect(_on_sdp.bind(1))
	pc2.session_description_created.connect(_on_sdp.bind(2))
	pc1.ice_candidate_created.connect(_on_ice.bind(1))
	pc2.ice_candidate_created.connect(_on_ice.bind(2))
	pc2.data_channel_received.connect(_on_dc)

	for entry in CONFIGS:
		var ch: WebRTCDataChannel = pc1.create_data_channel(entry[0], entry[1])
		out("created %s ok=%s" % [entry[0], str(ch != null)])
		if ch:
			local[entry[0]] = ch
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

func _on_dc(channel: WebRTCDataChannel) -> void:
	remote[channel.get_label()] = channel

func _all_open() -> bool:
	if remote.size() < CONFIGS.size():
		return false
	for entry in CONFIGS:
		var label: String = entry[0]
		if not local.has(label) or not remote.has(label):
			return false
		if local[label].get_ready_state() != WebRTCDataChannel.STATE_OPEN:
			return false
		if remote[label].get_ready_state() != WebRTCDataChannel.STATE_OPEN:
			return false
	return true

func _report(label: String, side: String, ch: WebRTCDataChannel) -> void:
	out("%s %s ordered=%s lifetime=%d rtx=%d" % [
		label, side, str(ch.is_ordered()), ch.get_max_packet_life_time(),
		ch.get_max_retransmits()])

func _step() -> void:
	match phase:
		"connecting":
			if _all_open():
				out("CONNECTED after %d ticks" % ticks)
				for entry in CONFIGS:
					var label: String = entry[0]
					_report(label, "local ", local[label])
					_report(label, "remote", remote[label])
				# The question in one line, independent of how "unset" prints.
				var typo_set: bool = local["typo"].get_max_packet_life_time() == 1
				var correct_set: bool = local["correct"].get_max_packet_life_time() == 1
				out("lifetime_applied typo=%s correct=%s" % [str(typo_set), str(correct_set)])
				out("chancfg verdict=%s" % ("BOTH_APPLIED" if typo_set and correct_set \
						else ("TYPO_DROPPED" if correct_set else "NEITHER_APPLIED")))
				phase = "done"
			elif ticks > 1200:
				out("TIMEOUT local=%d remote=%d" % [local.size(), remote.size()])
				phase = "done"

func _process(_delta: float) -> void:
	if phase == "done":
		return
	ticks += 1
	pc1.poll()
	pc2.poll()
	for ch in local.values():
		ch.poll()
	for ch in remote.values():
		ch.poll()
	_step()
	if ticks >= 2000 and phase != "done":
		out("TIMEOUT phase=%s" % phase)
		phase = "done"
	if phase == "done":
		finish()
