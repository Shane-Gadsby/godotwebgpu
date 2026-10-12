# F1 behavioral probe: how each platform handles payloads above the documented
# per-packet limit. `get_max_packet_size()` is NOT bound to GDScript (it is a
# PacketPeer C++ virtual, and neither PacketPeer nor WebRTCDataChannel binds
# it), so the only way to compare 1200 (web) against 16384 (native) from a
# script is to send across the boundary and watch.
#
# Reads per size: put_packet()'s error code, the channel's state right after the
# send, and whether the peer received the payload intact.
#
# NB sizes above the receive RingBuffer also hit F3 on web, so a divergence
# there is F3, not F1. The exact web ceiling is 65530 bytes with the default
# `network/limits/webrtc/max_channel_in_buffer_kb` of 64: RingBuffer::space_left()
# caps at size - 1, and `_on_message` needs p_size + 5 for the length/type
# header. Everything at or below 16 KB is F1's territory alone.
extends "res://tests/rtc_test_base.gd"

var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection
var ch1: WebRTCDataChannel
var ch2: WebRTCDataChannel

# Straddles both documented limits (1200 web / 16384 native) and both sides of
# web's exact 65530-byte receive ceiling, so one run separates F1 from F3.
const SIZES := [1200, 1201, 4096, 16384, 16385, 65530, 65531, 262144]

var size_idx := 0
var sent_tick := -1
var sent_err := -1

func _ready() -> void:
	phase = "connecting"
	pc1 = WebRTCPeerConnection.new()
	pc2 = WebRTCPeerConnection.new()
	out("impl=%s" % pc1.get_class())
	out("init1=%d init2=%d" % [pc1.initialize({}), pc2.initialize({})])

	pc1.session_description_created.connect(_on_sdp.bind(1))
	pc2.session_description_created.connect(_on_sdp.bind(2))
	pc1.ice_candidate_created.connect(_on_ice.bind(1))
	pc2.ice_candidate_created.connect(_on_ice.bind(2))
	pc2.data_channel_received.connect(_on_dc)

	ch1 = pc1.create_data_channel("sizes", { "ordered": true })
	out("ch1_created=%s" % str(ch1 != null))
	# Not callable from GDScript on either platform; recorded so the reason this
	# fixture is behavioral stays visible in the output itself.
	out("max_packet_size_bound=%s" % str(ch1.has_method("get_max_packet_size")))
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
	ch2 = channel

func _payload(n: int) -> PackedByteArray:
	var p := PackedByteArray()
	p.resize(n)
	for i in range(n):
		p[i] = (i * 7 + 13) % 251
	return p

func _matches(p: PackedByteArray, n: int) -> bool:
	if p.size() != n:
		return false
	# Spot-check rather than compare 256 KB byte by byte: the ends and the seam
	# are what a truncating or re-framing bug moves.
	for i in [0, 1, n / 2, n - 2, n - 1]:
		if p[i] != (i * 7 + 13) % 251:
			return false
	return true

func _send_next() -> void:
	if size_idx >= SIZES.size():
		out("sizes done")
		phase = "done"
		return
	var n: int = SIZES[size_idx]
	# A channel aborted by an earlier oversized send cannot carry the rest; say
	# so instead of reporting the remaining sizes as plain send failures.
	if ch1.get_ready_state() != WebRTCDataChannel.STATE_OPEN:
		out("size=%d SKIPPED ch1_state=%d (channel died on an earlier send)" % [
			n, ch1.get_ready_state()])
		size_idx += 1
		_send_next()
		return
	sent_err = ch1.put_packet(_payload(n))
	out("size=%d put_err=%d ch1_state_after=%d" % [n, sent_err, ch1.get_ready_state()])
	# "curve": how much is still queued right after the send is legitimately
	# platform- and run-dependent, so compare.sh collapses it. See
	# webgpu_notes/webrtc_web.md §4.3 and tests/buffered.gd.
	out("curve size=%d buffered_after_send=%d" % [n, ch1.get_buffered_amount()])
	sent_tick = ticks
	phase = "recv"

func _step() -> void:
	match phase:
		"connecting":
			if ch1 and ch2 and ch1.get_ready_state() == WebRTCDataChannel.STATE_OPEN \
					and ch2.get_ready_state() == WebRTCDataChannel.STATE_OPEN:
				out("CONNECTED after %d ticks" % ticks)
				ch1.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
				_send_next()

		"recv":
			var n: int = SIZES[size_idx]
			if ch2.get_available_packet_count() > 0:
				var p := ch2.get_packet()
				out("size=%d rx_len=%d intact=%s verdict=DELIVERED" % [
					n, p.size(), str(_matches(p, n))])
				size_idx += 1
				_send_next()
			elif ticks - sent_tick > 200:
				out("size=%d rx_len=0 intact=false verdict=%s" % [
					n, "NOT_SENT" if sent_err != OK else "DROPPED"])
				size_idx += 1
				_send_next()

func _process(_delta: float) -> void:
	if phase == "done":
		return
	ticks += 1
	pc1.poll()
	pc2.poll()
	if ch1:
		ch1.poll()
	if ch2:
		ch2.poll()
	_step()
	if ticks >= 2500 and phase != "done":
		out("TIMEOUT in phase=%s size_idx=%d" % [phase, size_idx])
		phase = "done"
	if phase == "done":
		finish()
