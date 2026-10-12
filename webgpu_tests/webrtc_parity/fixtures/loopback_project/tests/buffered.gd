# Settles the get_buffered_amount() question (webrtc_web.md §4.3): read right
# after a one-tick burst, web reports the whole payload and native reports 0.
#
# Both are correct, and this fixture is built to show why. 10 MB is queued in a
# single tick, then the sender's get_buffered_amount() and the receiver's packet
# count are sampled together every tick. What each platform reports is
# legitimately different — libdatachannel's send() writes straight into usrsctp
# and only queues when that would block, which an in-process peer never makes it
# do, whereas the browser queues everything and drains it on the event loop the
# script is currently monopolising — so the two `curve` lines below are an
# EXPECTED divergence in ./compare.sh, not a regression.
#
# What must match, and is what this fixture actually guards:
#   in_range  every sample is between 0 and the total queued, i.e. the value is
#             always a plausible account of this end's own queue
#   final     everything sent arrives, in order, and the sender settles at 0
#
# A tempting third check is NOT an invariant and is reported as data instead:
# that the sender never claims to owe more than the receiver is missing. Web
# violates it (by up to ~1 MB here) because in a one-page loopback the receiver
# can see messages before the sender's own bufferedAmount has been decremented.
# The two counters are updated independently with no happens-before relation, so
# get_buffered_amount() only means anything about the end you read it from.
#
# A native run at 20x this payload (200 MB, scratch, not committed because the
# browser's 16 MB send-buffer limit makes it meaningless on web) reports
# buffered=1060000 on the send tick, and native at THIS payload reports anywhere
# from 0 to 3.4 MB run to run: native does account for a backlog, it just
# rarely has much of one against a peer in its own process.
extends "res://tests/rtc_test_base.gd"

var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection
var ch1: WebRTCDataChannel
var ch2: WebRTCDataChannel

const COUNT := 1000
const SIZE := 10000  # 10 MB total.

var sent := 0
var send_errors := 0
var peak_buffered := 0
var samples := 0
var out_of_range := 0
var overshoot := 0
var zero_seen := false
var send_tick := -1

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
	ch1 = pc1.create_data_channel("buffered", { "ordered": true })
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

# One paired reading of both ends.
func _sample() -> Array:
	var buffered := ch1.get_buffered_amount()
	var received: int = ch2.get_available_packet_count()
	samples += 1
	peak_buffered = max(peak_buffered, buffered)
	if buffered < 0 or buffered > COUNT * SIZE:
		out_of_range += 1
	# Not an invariant — see the header. Recorded to show by how much the two
	# ends' counters can disagree about who holds the data.
	overshoot = max(overshoot, buffered - (sent - received) * SIZE)
	return [buffered, received]

func _step() -> void:
	match phase:
		"connecting":
			if ch1 and ch2 and ch1.get_ready_state() == WebRTCDataChannel.STATE_OPEN \
					and ch2.get_ready_state() == WebRTCDataChannel.STATE_OPEN:
				out("CONNECTED after %d ticks; queueing %d x %d = %d bytes in one tick" % [
					ticks, COUNT, SIZE, COUNT * SIZE])
				ch1.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
				var b := PackedByteArray()
				b.resize(SIZE)
				for i in range(COUNT):
					b.encode_u32(0, i)
					if ch1.put_packet(b) == OK:
						sent += 1
					else:
						send_errors += 1
				out("sent=%d send_errors=%d" % [sent, send_errors])
				var s := _sample()
				# EXPECTED to differ per platform; see the header.
				out("curve same_tick buffered=%d receiver_has=%d of %d" % [s[0], s[1], COUNT])
				send_tick = ticks
				phase = "watch"

		"watch":
			var s := _sample()
			if s[0] == 0 and not zero_seen:
				zero_seen = true
				# EXPECTED to differ per platform; see the header.
				out("curve buffered_zero_after %d ticks, receiver_has=%d of %d" % [
					ticks - send_tick, s[1], COUNT])
			if s[1] >= sent and zero_seen:
				phase = "drain"
			elif ticks - send_tick > 1200:
				out("TIMEOUT receiver_has=%d of %d buffered=%d" % [s[1], sent, s[0]])
				phase = "drain"

		"drain":
			var drained := 0
			var ordered := true
			while ch2.get_available_packet_count() > 0:
				var p := ch2.get_packet()
				if p.size() != SIZE or p.decode_u32(0) != drained:
					ordered = false
				drained += 1
			out("in_range=%s (out_of_range=%d samples)" % [str(out_of_range == 0), out_of_range])
			out("curve ends_disagree_by_at_most=%d bytes" % max(overshoot, 0))
			out("final arrived=%s in_order=%s buffered_settled=%s" % [
				str(drained == sent), str(ordered), str(ch1.get_buffered_amount() == 0)])
			out("buffered verdict=%s" % ("CONSISTENT" if out_of_range == 0 and drained == sent \
					and ordered and ch1.get_buffered_amount() == 0 else "INCONSISTENT"))
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
		out("TIMEOUT phase=%s" % phase)
		phase = "done"
	if phase == "done":
		finish()
