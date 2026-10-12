# F3 regression guard: repeated grow / partial-drain / regrow cycles on one
# reliable-ordered channel.
#
# `burst.gd` overruns the receive buffer once, from empty. This fixture attacks
# the case that one cannot reach: growing a buffer whose read position is
# already in the middle, so the queued data is wrapped when it is moved.
# `RingBuffer::resize()` has a separate branch for that (`read_pos > write_pos`),
# and getting it wrong corrupts or reorders packets rather than losing them.
#
# Each round sends a batch, drains only half of it, then sends a larger batch on
# top — which both wraps and grows — and finally drains everything. Rounds
# double in size so every round has to grow past the previous round's peak, and
# the full drain in between lets the buffer shrink back to its configured size,
# so the shrink path is exercised too.
#
# Sequence numbers are global and strictly increasing across the whole run, so
# any loss, duplication or reordering shows up as a non-zero counter.
extends "res://tests/rtc_test_base.gd"

var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection
var ch1: WebRTCDataChannel
var ch2: WebRTCDataChannel

const ROUNDS := 3
const BASE_A := 100  # first batch of a round
const BASE_B := 200  # second batch, sent on top of the undrained half
const SIZE := 1000

var round_idx := 0
var batch_a := 0
var batch_b := 0
var sent := 0
var received := 0
var loss := 0
var out_of_order := 0
var dupes := 0
var next_seq := 0
var expect_seq := 0
var wait_start := -1
var want_avail := 0

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
	ch1 = pc1.create_data_channel("grow", { "ordered": true })
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

func _send(count: int) -> void:
	for _i in range(count):
		var b := PackedByteArray()
		b.resize(SIZE)
		b.encode_u32(0, next_seq)
		if ch1.put_packet(b) == OK:
			sent += 1
			next_seq += 1

# Drains up to p_max packets, checking the global sequence as it goes.
func _drain(p_max: int) -> int:
	var n := 0
	while n < p_max and ch2.get_available_packet_count() > 0:
		var p := ch2.get_packet()
		if p.size() != SIZE:
			break
		var seq := p.decode_u32(0)
		received += 1
		n += 1
		if seq < expect_seq:
			dupes += 1
		elif seq > expect_seq:
			loss += seq - expect_seq
			out_of_order += 1
		expect_seq = seq + 1
	return n

func _begin_round() -> void:
	batch_a = BASE_A << round_idx
	batch_b = BASE_B << round_idx
	out("round=%d batch_a=%d batch_b=%d bytes=%d" % [
		round_idx, batch_a, batch_b, (batch_a + batch_b) * SIZE])
	_send(batch_a)
	want_avail = batch_a
	wait_start = ticks
	phase = "wait_a"

func _step() -> void:
	match phase:
		"connecting":
			if ch1 and ch2 and ch1.get_ready_state() == WebRTCDataChannel.STATE_OPEN \
					and ch2.get_ready_state() == WebRTCDataChannel.STATE_OPEN:
				out("CONNECTED after %d ticks" % ticks)
				ch1.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
				_begin_round()

		"wait_a":
			if ch2.get_available_packet_count() >= want_avail:
				# Half out, half left behind: the read position is now in the
				# middle of the buffer, so the next batch wraps around it.
				var half := batch_a / 2
				out("round=%d drained_half=%d left=%d" % [
					round_idx, _drain(half), ch2.get_available_packet_count()])
				_send(batch_b)
				want_avail = (batch_a - half) + batch_b
				wait_start = ticks
				phase = "wait_b"
			elif ticks - wait_start > 600:
				out("round=%d TIMEOUT waiting for batch_a (avail=%d want=%d)" % [
					round_idx, ch2.get_available_packet_count(), want_avail])
				phase = "verdict"

		"wait_b":
			if ch2.get_available_packet_count() >= want_avail:
				out("round=%d avail_after_wrap=%d" % [round_idx, ch2.get_available_packet_count()])
				_drain(want_avail)
				out("round=%d fully_drained total_sent=%d total_received=%d" % [
					round_idx, sent, received])
				round_idx += 1
				if round_idx >= ROUNDS:
					phase = "verdict"
				else:
					_begin_round()
			elif ticks - wait_start > 600:
				out("round=%d TIMEOUT waiting for batch_b (avail=%d want=%d)" % [
					round_idx, ch2.get_available_packet_count(), want_avail])
				phase = "verdict"

		"verdict":
			out("sent=%d received=%d loss=%d out_of_order=%d dupes=%d" % [
				sent, received, loss, out_of_order, dupes])
			out("grow verdict=%s" % ("LOSSLESS" if sent == received and loss == 0 \
					and out_of_order == 0 and dupes == 0 else "BROKEN"))
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
	if ticks >= 4000 and phase != "done":
		out("TIMEOUT phase=%s round=%d sent=%d received=%d" % [phase, round_idx, sent, received])
		phase = "done"
	if phase == "done":
		finish()
