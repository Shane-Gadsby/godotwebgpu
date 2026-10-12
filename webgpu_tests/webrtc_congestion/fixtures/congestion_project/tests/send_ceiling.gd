# Pushes into the send queue as hard as it will take, over a link far too slow
# to drain it, to find out whether there is a ceiling and where.
#
# This is webrtc_web.md §4.4 unknown 2, and the one most likely to be a REAL
# DIVERGENCE: Chrome is documented to cap a data channel's send buffer
# (historically 16 MiB) and to fail or close the channel past it, while
# libdatachannel's mSendQueue has no equivalent cap. backpressure.gd cannot
# reach it -- it offers 3 MB in total, so the queue never approaches 16 MiB --
# and on a fast loopback the queue never builds at all.
#
# Since the F1 fix an oversized send returns FAILED rather than vanishing, so a
# cap is observable. The questions, in order:
#   1. Is there a cap at all, and at what buffered figure?
#   2. Is hitting it GRACEFUL -- put_packet() returns FAILED and the channel
#      stays open -- or FATAL, closing the channel and losing the stream?
#   3. Does the sender RECOVER once the link drains, i.e. can an application
#      treat FAILED as "retry later"?
#
# Question 3 is why this fixture does not stop at the first failure: it backs
# off, waits for the queue to drain, and retries. A cap an application can
# recover from is a documentation note; one that kills the channel is a bug.
#
# Everything accepted must still arrive intact, so a cap cannot be confused
# with data loss.
extends "res://tests/cong_base.gd"

const SIZE := 16000           # Large packets: fill the queue in fewer calls.
const MAX_ACCEPTED := 24000000  # Stop at 24 MB even if no cap appears (> 16 MiB).
const DRAIN_TARGET := 2000000   # Retry once the queue falls below 2 MB.
const SAMPLE_S := 0.25
const SENTINEL := 0xFFFFFFFF

var ch: WebRTCDataChannel

# Offerer.
var sent := 0
var fails := 0
var first_fail_buffered := -1
var first_fail_sent := -1
var peak_buffered := 0
var closed_on_fail := false
var state_at_fail := -1
var recovered := false
var retry_sent_before := -1
var last_sample := -1.0
var backoff_at := -1.0
var curve := []
var report := {}

# Answerer.
var recv := 0
var recv_bytes := 0
var gaps := 0
var next_expected := 0
var reported := false
var reported_at := -1.0

func budget_s() -> float:
	# Draining ~16 MiB over a deliberately slow link is the slow part, so this
	# is generous. Run it at --rate 8mbit if you want it to finish sooner.
	return 420.0

func _channels() -> Array:
	return [ch]

func _create_channels() -> void:
	ch = pc.create_data_channel("send_ceiling", {"ordered": true})

func _on_data_channel(c: WebRTCDataChannel) -> void:
	ch = c
	out("channel received label=%s" % c.get_label())

func _open() -> bool:
	return ch != null and ch.get_ready_state() == WebRTCDataChannel.STATE_OPEN

func _sample() -> void:
	var b := ch.get_buffered_amount()
	if b > peak_buffered:
		peak_buffered = b
	curve.append("%.1f:%d" % [now(), b])

# ─── Offerer ──────────────────────────────────────────────────────────────────

# Pushes until the queue refuses or the cap-probe limit is reached. No pacing:
# the point is to outrun the link by as much as possible.
func _push_hard() -> void:
	var payload := PackedByteArray()
	payload.resize(SIZE)
	# Bounded per tick so the main loop keeps turning -- an unbounded loop here
	# would starve the very callbacks that drain the queue, and the "ceiling"
	# found would be the fixture's own doing.
	for _i in range(64):
		if sent * SIZE >= MAX_ACCEPTED:
			return
		payload.encode_u32(0, sent)
		if ch.put_packet(payload) == OK:
			sent += 1
		else:
			fails += 1
			if first_fail_buffered < 0:
				first_fail_buffered = ch.get_buffered_amount()
				first_fail_sent = sent
				state_at_fail = ch.get_ready_state()
				closed_on_fail = state_at_fail != WebRTCDataChannel.STATE_OPEN
				out("FIRST FAILURE at t=%.2f: accepted=%d bytes buffered=%d state=%d closed=%s" % [
					now(), sent * SIZE, first_fail_buffered, state_at_fail, str(closed_on_fail)])
			return

func _finish_offering(why: String) -> void:
	# Sample before reporting: the push can finish inside one sample interval
	# (native accepted 24 MB in 0.17s), which printed peak_buffered=0 here next
	# to a non-zero peak in the verdict -- two contradictory numbers in one log.
	_sample()
	out("stopped offering (%s): accepted=%d packets = %d bytes, fails=%d, peak_buffered=%d" % [
		why, sent, sent * SIZE, fails, peak_buffered])
	# The sentinel tells the answerer the stream is over. It has to get in, so
	# keep trying while the queue drains.
	phase = "sentinel"

func _try_sentinel() -> void:
	var p := PackedByteArray()
	p.resize(SIZE)
	p.encode_u32(0, SENTINEL)
	if ch.put_packet(p) == OK:
		out("sentinel queued at t=%.2f, draining %d bytes" % [now(), ch.get_buffered_amount()])
		phase = "draining"

func _verdict() -> void:
	var accepted: int = sent * SIZE
	var delivered: int = int(report.get("packets", -1))
	var intact: bool = delivered == sent and int(report.get("gaps", -1)) == 0
	out("peak_buffered=%d accepted=%d bytes fails=%d" % [peak_buffered, accepted, fails])
	if fails > 0:
		out("ceiling_at_buffered=%d after %d bytes accepted" % [first_fail_buffered, first_fail_sent * SIZE])
		out("graceful=%s (channel state at failure=%d, closed=%s)" % [
			str(not closed_on_fail), state_at_fail, str(closed_on_fail)])
		out("recovered_after_drain=%s" % str(recovered))
	else:
		out("no ceiling up to %d bytes accepted (peak queue %d)" % [accepted, peak_buffered])
	out("delivered=%d of %d accepted gaps=%d intact=%s" % [
		delivered, sent, int(report.get("gaps", -1)), str(intact)])
	out("curve series %s" % " ".join(curve))

	var verdict := "BROKEN"
	if peak_buffered == 0:
		# Same guard as backpressure.gd: with no shaping the link drains as fast
		# as the queue fills, so no ceiling can be reached and the run says
		# nothing. See shape.sh.
		verdict = "NO_PRESSURE"
	elif fails == 0:
		# Named for what was actually shown: no ceiling was reached BELOW the
		# probe limit. It is not evidence of an unbounded queue.
		verdict = "NO_CEILING_UP_TO_PROBE"
	elif not closed_on_fail and recovered and intact:
		verdict = "CEILING_GRACEFUL"
	elif closed_on_fail:
		verdict = "CEILING_FATAL"
	out("send_ceiling verdict=%s" % verdict)
	phase = "done"

# ─── Answerer ─────────────────────────────────────────────────────────────────

func _drain() -> void:
	while ch != null and ch.get_available_packet_count() > 0:
		var p := ch.get_packet()
		if p.size() < 4:
			continue
		var idx := p.decode_u32(0)
		if idx == SENTINEL:
			_report()
			return
		if idx != next_expected:
			gaps += 1
		next_expected = int(idx) + 1
		recv += 1
		recv_bytes += p.size()

func _report() -> void:
	if reported:
		return
	reported = true
	out("received %d packets (%d bytes) gaps=%d by t=%.2f" % [recv, recv_bytes, gaps, now()])
	_send_signal({"type": "report", "packets": recv, "bytes": recv_bytes, "gaps": gaps})
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
		elif phase in ["pushing", "backoff", "sentinel"] and ch != null:
			# The channel went away mid-run. That is itself the answer to
			# question 2, so record it rather than timing out.
			out("CHANNEL CLOSED mid-run at t=%.2f state=%d" % [now(), ch.get_ready_state()])
			closed_on_fail = true
			_verdict()
		return

	if role != "offerer":
		if phase == "connecting":
			out("CONNECTED at t=%.2fs, draining as fast as it arrives" % now())
			phase = "receiving"
		_drain()
		if phase == "reported" and now() - reported_at > 2.0:
			phase = "done"
		return

	if now() - last_sample >= SAMPLE_S:
		last_sample = now()
		_sample()

	match phase:
		"connecting":
			out("CONNECTED at t=%.2fs; pushing up to %d bytes with no pacing" % [now(), MAX_ACCEPTED])
			ch.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
			last_sample = now()
			phase = "pushing"

		"pushing":
			_push_hard()
			if fails > 0:
				# Back off and let the link drain, to find out whether FAILED is
				# recoverable or terminal.
				out("backing off at t=%.2f, waiting for the queue to fall below %d" % [
					now(), DRAIN_TARGET])
				retry_sent_before = sent
				backoff_at = now()
				phase = "backoff"
			elif sent * SIZE >= MAX_ACCEPTED:
				_finish_offering("probe limit reached, no ceiling found")

		"backoff":
			if ch.get_buffered_amount() <= DRAIN_TARGET:
				_push_hard()
				recovered = sent > retry_sent_before
				out("after drain: retry accepted %d more packets, recovered=%s (t=%.2f)" % [
					sent - retry_sent_before, str(recovered), now()])
				_finish_offering("ceiling found and probed")
			elif now() - backoff_at > 240.0:
				out("TIMEOUT draining after the ceiling; buffered=%d" % ch.get_buffered_amount())
				_finish_offering("drain timed out")

		"sentinel":
			_try_sentinel()

		"draining":
			if now() > budget_s() - 20.0:
				out("TIMEOUT waiting for the peer's report")
				_verdict()
