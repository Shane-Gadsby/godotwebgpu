# Offers data faster than the link can carry it, for long enough that a backlog
# must build, and watches both the sender's queue and what actually arrives.
#
# This is the fixture webrtc_web.md §4.4 unknowns 1 and 2 are waiting on:
#
#   1. Does native's get_buffered_amount() TRACK a real backlog? §4.3 proved it
#      can report one (0 -> 3.4 MB run to run on a loopback), but not that the
#      number rises with offered load and falls as the link drains. It is the
#      only flow-control signal in Godot's WebRTC API, so a project that
#      throttles on it depends on exactly this property.
#   2. Does web hit a send-queue ceiling that native does not? Chrome is
#      documented to cap a data channel's send buffer (historically 16 MiB) and
#      to fail or close the channel past it; libdatachannel's mSendQueue has no
#      equivalent cap. Since the F1 fix an oversized send returns FAILED instead
#      of vanishing, so if the cap exists this fixture sees it as send_failed>0.
#
# Shape of the run: the offerer pushes OFFER_BPS bytes/second (well above the
# shaped rate) until TOTAL_BYTES have been accepted, then stops and lets the
# link drain. Both ends are sampled on a wall-clock interval, never per tick,
# because a congested run has no stable frame rate.
#
# The answerer drains every tick and, on the end-of-stream sentinel, reports its
# totals back over the signaling socket so ONE verdict can be computed from both
# ends. Its report is a few hundred bytes at the very end of the run, so it does
# not meaningfully compete with the measurement for bandwidth.
extends "res://tests/cong_base.gd"

const SIZE := 1000
const TOTAL_BYTES := 3000000
const OFFER_BPS := 500000.0   # 4 Mbit/s offered; run it over a 1-2 Mbit/s link.
const SAMPLE_S := 0.1
const SENTINEL := 0xFFFFFFFF
const DRAIN_GRACE_S := 60.0

var ch: WebRTCDataChannel

# Offerer state.
var sent := 0
var send_failed := 0
var first_fail_buffered := -1
var first_fail_at := -1.0
var offer_started := -1.0
var offer_done_at := -1.0
var last_sample := -1.0
var peak_buffered := 0
var samples := 0
var rising_seen := false
var drained_to_zero_at := -1.0
var report := {}
var curve := []

# Answerer state.
var recv_packets := 0
var recv_bytes := 0
var recv_gaps := 0
var next_expected := 0
var first_recv_at := -1.0
var last_recv_at := -1.0
var reported := false
var reported_at := -1.0

func budget_s() -> float:
	return 240.0

func _channels() -> Array:
	return [ch]

func _create_channels() -> void:
	# Reliable and ordered: the point is to build a queue, not to lose packets.
	ch = pc.create_data_channel("backpressure", {"ordered": true})

func _on_data_channel(c: WebRTCDataChannel) -> void:
	ch = c
	out("channel received label=%s" % c.get_label())

func _open() -> bool:
	return ch != null and ch.get_ready_state() == WebRTCDataChannel.STATE_OPEN

# ─── Sampling ─────────────────────────────────────────────────────────────────

func _sample(tag: String) -> void:
	var buffered := ch.get_buffered_amount()
	samples += 1
	if buffered > peak_buffered:
		peak_buffered = buffered
	# "Rising" means the queue grew past a packet or two, i.e. the link really
	# is the bottleneck and the counter noticed. Without this the verdict could
	# not tell a tracked backlog from a counter that only ever flickers.
	if buffered > SIZE * 10:
		rising_seen = true
	if buffered == 0 and offer_done_at >= 0.0 and drained_to_zero_at < 0.0:
		drained_to_zero_at = now()
	curve.append("%.1f:%d" % [now(), buffered])
	if tag != "":
		out("curve %s t=%.2f buffered=%d sent=%d" % [tag, now(), buffered, sent])

# ─── Offerer ──────────────────────────────────────────────────────────────────

func _offer_more() -> void:
	# Push whatever the configured offered rate allows since the last tick, so
	# the sender's pressure is wall-clock-paced rather than frame-paced.
	var elapsed := now() - offer_started
	var allowed := int(elapsed * OFFER_BPS)
	var payload := PackedByteArray()
	payload.resize(SIZE)
	while sent * SIZE < min(allowed, TOTAL_BYTES):
		payload.encode_u32(0, sent)
		if ch.put_packet(payload) == OK:
			sent += 1
		else:
			send_failed += 1
			if first_fail_buffered < 0:
				first_fail_buffered = ch.get_buffered_amount()
				first_fail_at = now()
				out("send_failed first at t=%.2f after %d packets, buffered=%d" % [
					first_fail_at, sent, first_fail_buffered])
			# Back off for this tick: hammering a full queue produces thousands
			# of identical failures and tells us nothing more.
			return

func _send_sentinel() -> void:
	var p := PackedByteArray()
	p.resize(SIZE)
	p.encode_u32(0, SENTINEL)
	# The sentinel must arrive, and it is the one packet whose loss would stall
	# the whole run, so retry it until the queue accepts it.
	if ch.put_packet(p) == OK:
		offer_done_at = now()
		out("offered %d packets (%d bytes) by t=%.2f, send_failed=%d" % [
			sent, sent * SIZE, offer_done_at, send_failed])
		phase = "draining"

func _verdict() -> void:
	# Tracking means the counter both rose under load and came back to zero:
	# either half alone is consistent with a counter that does not work.
	var tracks: bool = rising_seen and drained_to_zero_at >= 0.0
	var delivered: int = int(report.get("packets", -1))
	var gaps: int = int(report.get("gaps", -1))
	var all_arrived: bool = delivered == sent
	out("peak_buffered=%d samples=%d rose=%s drained_to_zero=%s" % [
		peak_buffered, samples, str(rising_seen), str(drained_to_zero_at >= 0.0)])
	out("buffered_tracks_backlog=%s" % str(tracks))
	out("send_failed=%d first_fail_buffered=%d" % [send_failed, first_fail_buffered])
	out("delivered=%d of %d sent gaps=%d lossless=%s" % [delivered, sent, gaps, str(all_arrived)])
	out("goodput_bps=%d over %.2fs" % [
		int(float(report.get("bytes", 0)) / max(now(), 0.001)), now()])
	# The series is run- and link-specific, so compare.sh-style diffing must not
	# see it; it is the actual evidence for the tracking claim, though, so it is
	# printed rather than only summarized.
	out("curve series %s" % " ".join(curve))
	# NO_PRESSURE is the single most important verdict here. An unshaped link
	# keeps up with any offered rate, so buffered stays 0 and "does the counter
	# track a backlog" is UNANSWERED rather than answered no. Without this a
	# forgotten shape.sh looks exactly like a broken counter -- the §4.4 caveat
	# "verify with the delivery counts, not by assumption", made structural.
	var verdict := "BROKEN"
	if peak_buffered == 0 and all_arrived and gaps == 0 and send_failed == 0:
		verdict = "NO_PRESSURE"
	elif send_failed > 0:
		verdict = "CEILING_HIT"
	elif tracks and all_arrived and gaps == 0:
		verdict = "TRACKED"
	out("backpressure verdict=%s" % verdict)
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
		if first_recv_at < 0.0:
			first_recv_at = now()
		last_recv_at = now()
		if idx != next_expected:
			recv_gaps += 1
		next_expected = int(idx) + 1
		recv_packets += 1
		recv_bytes += p.size()

func _report() -> void:
	if reported:
		return
	reported = true
	out("received %d packets (%d bytes) gaps=%d between t=%.2f and %.2f" % [
		recv_packets, recv_bytes, recv_gaps, first_recv_at, last_recv_at])
	_send_signal({
		"type": "report", "packets": recv_packets, "bytes": recv_bytes,
		"gaps": recv_gaps, "first_at": first_recv_at, "last_at": last_recv_at,
	})
	reported_at = now()
	phase = "reported"

func _on_signal(msg: Dictionary) -> void:
	if str(msg.get("type", "")) == "report":
		report = msg
		out("peer report: packets=%d bytes=%d gaps=%d" % [
			int(msg.get("packets", -1)), int(msg.get("bytes", -1)), int(msg.get("gaps", -1))])
		_verdict()
		return
	super._on_signal(msg)

func _on_peer_bye() -> void:
	# The offerer only says bye after printing its verdict, by which point the
	# answerer has already reported. Finishing here is correct and keeps the
	# answerer from sitting out its whole budget.
	if role != "offerer":
		finish()

# ─── Drive ────────────────────────────────────────────────────────────────────

func _step() -> void:
	if not _open():
		if phase == "connecting" and now() > 45.0:
			out("TIMEOUT never opened; channel state=%s" % (
				"null" if ch == null else str(ch.get_ready_state())))
			phase = "done"
		return

	if role != "offerer":
		if phase == "connecting":
			out("CONNECTED at t=%.2fs, draining as fast as it arrives" % now())
			phase = "receiving"
		_drain()
		# Do not wait for the offerer's bye: on a slow link it can be delayed
		# behind the queue, and the answerer has nothing left to do once its
		# report is sent. The grace is only to let the report flush.
		if phase == "reported" and now() - reported_at > 2.0:
			phase = "done"
		return

	match phase:
		"connecting":
			out("CONNECTED at t=%.2fs; offering %d bytes at %d B/s" % [
				now(), TOTAL_BYTES, int(OFFER_BPS)])
			ch.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
			offer_started = now()
			last_sample = now()
			phase = "offering"
			_sample("start")

		"offering":
			_offer_more()
			if now() - last_sample >= SAMPLE_S:
				last_sample = now()
				_sample("")
			if sent * SIZE >= TOTAL_BYTES:
				_send_sentinel()

		"draining":
			if now() - last_sample >= SAMPLE_S:
				last_sample = now()
				_sample("")
			if now() - offer_done_at > DRAIN_GRACE_S:
				out("TIMEOUT draining; no peer report after %.0fs" % DRAIN_GRACE_S)
				_verdict()
