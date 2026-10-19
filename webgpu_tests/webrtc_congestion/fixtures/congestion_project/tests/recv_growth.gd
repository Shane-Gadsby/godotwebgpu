# Fills the RECEIVING end's buffer, over a slow link, with a peer that refuses
# to drain, and watches how far it grows.
#
# This is webrtc_web.md §4.4 unknown 3. F3's fix made web's receive RingBuffer
# GROW instead of dropping packets, with no ceiling, on the strength of native
# doing the same -- a decision taken against a loopback where the buffer peaks
# at the burst size and shrinks straight back. The open questions were:
#
#   1. How large does it actually get when the link is slow and the poller is
#      slower?
#   2. Does the 8 MiB warning fire at a useful point? (It is a one-shot WARN
#      from webrtc_data_channel_js.cpp, so look for it in the runner's log, not
#      in this fixture's output -- GDScript cannot observe it.)
#   3. Does shrink-on-empty ever get a chance to run?
#
# NB this characterizes WEB. The growth is web's mechanism: the base class and
# its `network/limits/webrtc/max_channel_in_buffer_kb` setting are only used by
# the JS implementation, because the desktop extension keeps its own unbounded
# queue and ignores the setting entirely (see F3). A native run is the
# comparison, not the subject.
#
# Shape of the run: the offerer sends a link-paced stream; the answerer polls
# the channel every tick -- which is what moves packets into the buffer -- but
# calls get_packet() on NONE of them until HOLD_S has passed. Then it drains
# everything at once and checks that nothing was lost or reordered on the way,
# which is the part F3's fix could plausibly get wrong when a wrapped ring
# buffer is resized.
extends "res://tests/cong_base.gd"

const SIZE := 4000
const TOTAL := 3000            # 12 MB, comfortably past the 8 MiB warning.
const HOLD_S := 25.0           # Hold without draining.
const SAMPLE_S := 0.5
const SENTINEL := 0xFFFFFFFF

var ch: WebRTCDataChannel

# Offerer.
var sent := 0
var send_failed := 0
var report := {}

# Answerer.
var peak_available := 0
var peak_bytes := 0
var held_until := -1.0
var drained := 0
var drain_gaps := 0
var next_expected := 0
var saw_sentinel := false
var reported := false
var reported_at := -1.0
var curve := []
var last_sample := -1.0
var avail_after_drain := -1

func budget_s() -> float:
	return 300.0

func _channels() -> Array:
	return [ch]

func _create_channels() -> void:
	ch = pc.create_data_channel("recv_growth", {"ordered": true})

func _on_data_channel(c: WebRTCDataChannel) -> void:
	ch = c
	out("channel received label=%s" % c.get_label())

func _open() -> bool:
	return ch != null and ch.get_ready_state() == WebRTCDataChannel.STATE_OPEN

# ─── Offerer ──────────────────────────────────────────────────────────────────

# Push as fast as the channel accepts, bounded per tick. The link, not the
# fixture, should be what paces this -- the receiving buffer only grows if data
# arrives faster than it is drained, and nothing is being drained at all.
func _push() -> void:
	var payload := PackedByteArray()
	payload.resize(SIZE)
	for _i in range(32):
		if sent >= TOTAL:
			return
		payload.encode_u32(0, sent)
		if ch.put_packet(payload) == OK:
			sent += 1
		else:
			send_failed += 1
			return

func _verdict() -> void:
	out("sent=%d packets (%d bytes) send_failed=%d" % [sent, sent * SIZE, send_failed])
	out("peer peak_held=%d packets = %d bytes" % [
		int(report.get("peak", -1)), int(report.get("peak_bytes", -1))])
	out("peer drained=%d gaps=%d avail_after_drain=%d" % [
		int(report.get("drained", -1)), int(report.get("gaps", -1)),
		int(report.get("avail_after", -1))])
	var intact: bool = int(report.get("drained", -1)) == sent and int(report.get("gaps", -1)) == 0
	var grew: bool = int(report.get("peak_bytes", 0)) > 1048576   # more than 1 MiB held
	out("held_over_1MiB=%s intact_after_growth=%s" % [str(grew), str(intact)])
	out("curve series %s" % str(report.get("curve", "")))
	var verdict := "BROKEN"
	if not grew:
		# Nothing accumulated, so nothing about growth was tested. Either the
		# link kept up or the hold did not hold.
		verdict = "NO_PRESSURE"
	elif intact:
		verdict = "GREW_INTACT"
	out("recv_growth verdict=%s" % verdict)
	phase = "done"

# ─── Answerer ─────────────────────────────────────────────────────────────────

func _sample_held() -> void:
	var n := ch.get_available_packet_count()
	if n > peak_available:
		peak_available = n
		peak_bytes = n * SIZE
	curve.append("%.1f:%d" % [now(), n * SIZE])

func _drain_all() -> void:
	while ch.get_available_packet_count() > 0:
		var p := ch.get_packet()
		if p.size() < 4:
			continue
		var idx := int(p.decode_u32(0))
		if idx == SENTINEL:
			saw_sentinel = true
			continue
		if idx != next_expected:
			drain_gaps += 1
		next_expected = idx + 1
		drained += 1

func _report() -> void:
	if reported:
		return
	reported = true
	# Read the count once more AFTER draining: F3's fix shrinks the buffer when
	# the queue empties, and a non-zero count here would mean the drain loop and
	# the counter disagree.
	avail_after_drain = ch.get_available_packet_count()
	out("peak_held=%d packets (%d bytes); drained=%d gaps=%d avail_after=%d" % [
		peak_available, peak_bytes, drained, drain_gaps, avail_after_drain])
	_send_signal({
		"type": "report", "peak": peak_available, "peak_bytes": peak_bytes,
		"drained": drained, "gaps": drain_gaps, "avail_after": avail_after_drain,
		"curve": " ".join(curve),
	})
	reported_at = now()
	phase = "reported"

func _on_signal(msg: Dictionary) -> void:
	if str(msg.get("type", "")) == "report":
		report = msg
		_verdict()
		return
	super._on_signal(msg)

# ─── Drive ────────────────────────────────────────────────────────────────────

func _step() -> void:
	if not _open():
		if phase == "connecting" and now() > 45.0:
			out("TIMEOUT never opened")
			phase = "done"
		return

	if role != "offerer":
		match phase:
			"connecting":
				held_until = now() + HOLD_S
				out("CONNECTED at t=%.2fs; holding without draining until t=%.2f" % [
					now(), held_until])
				last_sample = now()
				phase = "holding"
			"holding":
				# poll() still runs every tick from cong_base, which is what
				# moves arriving packets into the buffer. Nothing is drained.
				if now() - last_sample >= SAMPLE_S:
					last_sample = now()
					_sample_held()
				if now() >= held_until:
					_sample_held()
					out("hold over at t=%.2f with %d packets (%d bytes) held" % [
						now(), peak_available, peak_bytes])
					phase = "draining"
			"draining":
				_drain_all()
				# The sentinel is the offerer's end-of-stream marker; until it
				# arrives there is more to come, so keep draining.
				if saw_sentinel:
					_report()
			"reported":
				if now() - reported_at > 2.0:
					phase = "done"
		return

	match phase:
		"connecting":
			out("CONNECTED at t=%.2fs; pushing %d x %d B = %d bytes" % [
				now(), TOTAL, SIZE, TOTAL * SIZE])
			ch.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
			phase = "pushing"
		"pushing":
			_push()
			if sent >= TOTAL:
				out("all %d packets queued by t=%.2f (send_failed=%d)" % [sent, now(), send_failed])
				phase = "sentinel"
		"sentinel":
			var p := PackedByteArray()
			p.resize(SIZE)
			p.encode_u32(0, SENTINEL)
			if ch.put_packet(p) == OK:
				out("sentinel queued at t=%.2f" % now())
				phase = "waiting"
		"waiting":
			if now() > budget_s() - 20.0:
				out("TIMEOUT waiting for the peer's report")
				_verdict()
