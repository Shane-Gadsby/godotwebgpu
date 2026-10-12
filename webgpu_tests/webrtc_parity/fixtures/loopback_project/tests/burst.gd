# Throughput / ordering / backpressure stress on one reliable-ordered channel.
#
# Serves two purposes:
#  - stresses the threaded web build's `__proxy: 'sync'` marshalling far harder
#    than a three-packet handshake does;
#  - measures F3 directly. Every packet is sent without draining, then the
#    receiver is left to accumulate before reading, so the web build's fixed
#    64 KB RingBuffer (network/limits/webrtc/max_channel_in_buffer_kb) is
#    deliberately overrun, while native's unbounded std::queue is not.
extends "res://tests/rtc_test_base.gd"

var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection
var ch1: WebRTCDataChannel
var ch2: WebRTCDataChannel

const COUNT := 500
const SIZE := 1000  # bytes per packet; COUNT*SIZE = ~500 KB, far over the 64 KB buffer
const SETTLE_TICKS := 120

var sent := 0
var send_errors := 0
var received := 0
var out_of_order := 0
var first_gap := -1
var last_seq := -1
var settle_start := -1

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
	pc2.data_channel_received.connect(func(c): ch2 = c)
	ch1 = pc1.create_data_channel("burst", { "ordered": true })
	pc1.create_offer()

func _on_sdp(type: String, sdp: String, who: int) -> void:
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

func _make(seq: int) -> PackedByteArray:
	var b := PackedByteArray()
	b.resize(SIZE)
	b.encode_u32(0, seq)
	return b

func _step() -> void:
	match phase:
		"connecting":
			if ch1 and ch2 and ch1.get_ready_state() == WebRTCDataChannel.STATE_OPEN \
					and ch2.get_ready_state() == WebRTCDataChannel.STATE_OPEN:
				out("CONNECTED after %d ticks; sending %d x %d bytes in one tick" % [ticks, COUNT, SIZE])
				ch1.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
				for i in range(COUNT):
					if ch1.put_packet(_make(i)) == OK:
						sent += 1
					else:
						send_errors += 1
				out("sent=%d send_errors=%d buffered_after_send=%d" % [
					sent, send_errors, ch1.get_buffered_amount()])
				settle_start = ticks
				phase = "settle"

		"settle":
			# Deliberately do NOT drain: let the receive buffer fill up.
			if ticks - settle_start >= SETTLE_TICKS:
				out("settled %d ticks; avail_before_drain=%d" % [
					SETTLE_TICKS, ch2.get_available_packet_count()])
				phase = "drain"

		"drain":
			var drained_this_tick := 0
			while ch2.get_available_packet_count() > 0:
				var p := ch2.get_packet()
				if p.size() < 4:
					break
				var seq := p.decode_u32(0)
				received += 1
				drained_this_tick += 1
				if seq <= last_seq:
					out_of_order += 1
				elif seq != last_seq + 1 and first_gap == -1:
					first_gap = last_seq + 1
				last_seq = seq
			if drained_this_tick == 0:
				out("drained=%d of %d sent (loss=%d) out_of_order=%d first_gap=%d last_seq=%d" % [
					received, sent, sent - received, out_of_order, first_gap, last_seq])
				out("burst verdict=%s" % ("LOSSLESS" if received == sent and out_of_order == 0 else "LOSSY"))
				phase = "done"

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
	if ticks >= 3000 and phase != "done":
		out("TIMEOUT phase=%s sent=%d received=%d" % [phase, sent, received])
		phase = "done"
	if phase == "done":
		finish()
