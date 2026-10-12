# Sends the same paced stream down four differently-configured channels over a
# LOSSY link, and counts what arrives on each.
#
# This is webrtc_web.md §4.4 unknown 4, and it is the only test anywhere in this
# project that can show F8's fix DOES SOMETHING. The parity tier compares a
# channel's reported configuration (chancfg.gd) but never its behavior, because
# on a lossless loopback an unreliable channel is indistinguishable from a
# reliable one -- which is precisely how F8 (the `maxPacketLifetime` typo that
# made both unreliable transfer modes fully reliable on every platform) survived
# a clean parity diff for a whole round of work.
#
# Run it with loss:
#   ./shape.sh --rate 4mbit --delay 25ms --loss 5% -- \
#       env RTC_TEST=loss_modes node run_congestion.mjs
#
# The four channels, and what each is for:
#   reliable  {ordered:true}                      the control: must deliver 100%
#   rtx0      {ordered:true, maxRetransmits:0}    no retransmission at all
#   life50    {ordered:false, maxPacketLifeTime:50}  a realistic lifetime
#   life1     {ordered:true,  maxPacketLifeTime:1}  CH_ORDERED, i.e.
#             TRANSFER_MODE_UNRELIABLE_ORDERED, exactly as
#             WebRTCMultiplayerPeer::add_peer builds it (lifetime defaults to 1)
#   life1u    {ordered:false, maxPacketLifeTime:1}  CH_UNRELIABLE, i.e.
#             TRANSFER_MODE_UNRELIABLE, same default
#
# The two life1* channels are the interesting ones, because they are not
# hypothetical: they are byte-for-byte what every project using
# WebRTCMultiplayerPeer gets. life50 is the control for them -- same mechanism,
# a longer lifetime -- which is what makes it possible to say whether a
# divergence belongs to the lifetime VALUE or to the mechanism.
#
# What this measured, so nobody re-derives it: a 1 ms lifetime does NOT cause
# loss by itself. On a lossless 25 ms link every channel delivers 100% on both
# platforms, because the lifetime governs RETRANSMISSION, not first
# transmission. It bites only once the link actually drops something -- and
# there the two stacks diverge badly. See webrtc_web.md §4.4a.
#
# The stream is PACED well under the shaped rate on purpose: loss here must come
# from netem, not from a queue the fixture overfilled itself. A saturated link
# would make every channel lose packets and prove nothing about transfer modes.
extends "res://tests/cong_base.gd"

const SIZE := 1000
const COUNT := 200             # Per channel.
const SEND_HZ := 20.0          # Per channel; 4 x 20 x 1000 B/s = 80 KB/s.
const GRACE_S := 4.0           # Let in-flight packets land before the sentinel.
const SENTINEL := 0xFFFFFFFF

const LABELS := ["reliable", "rtx0", "life50", "life1", "life1u"]
const CONFIGS := [
	{"ordered": true},
	{"ordered": true, "maxRetransmits": 0},
	{"ordered": false, "maxPacketLifeTime": 50},
	{"ordered": true, "maxPacketLifeTime": 1},
	{"ordered": false, "maxPacketLifeTime": 1},
]

var chans := {}                # label -> WebRTCDataChannel
var sent := {}                 # label -> int
var recv := {}                 # label -> int
var out_of_order := {}         # label -> int
var last_idx := {}             # label -> int
var send_started := -1.0
var all_sent_at := -1.0
var sentinel_sent := false
var report := {}
var reported := false
var reported_at := -1.0

func budget_s() -> float:
	return 180.0

func _channels() -> Array:
	return chans.values()

func _init() -> void:
	for l in LABELS:
		sent[l] = 0
		recv[l] = 0
		out_of_order[l] = 0
		last_idx[l] = -1

func _create_channels() -> void:
	for i in range(LABELS.size()):
		var c := pc.create_data_channel(LABELS[i], CONFIGS[i])
		if c == null:
			out("FATAL could not create channel %s" % LABELS[i])
			phase = "done"
			return
		chans[LABELS[i]] = c
	out("created %d channels: %s" % [chans.size(), ", ".join(LABELS)])

func _on_data_channel(c: WebRTCDataChannel) -> void:
	chans[c.get_label()] = c
	out("channel received label=%s (%d of %d)" % [c.get_label(), chans.size(), LABELS.size()])

func _all_open() -> bool:
	if chans.size() < LABELS.size():
		return false
	for c in chans.values():
		if c.get_ready_state() != WebRTCDataChannel.STATE_OPEN:
			return false
	return true

func _report_config() -> void:
	# Proves the channels really are configured differently on THIS platform
	# before any conclusion is drawn from what they deliver. Without it a
	# "modes are identical" result could just as well mean the configuration
	# never applied -- which is the F8 failure mode exactly.
	for l in LABELS:
		var c: WebRTCDataChannel = chans[l]
		out("cfg %s ordered=%s lifetime=%d rtx=%d" % [
			l, str(c.is_ordered()), c.get_max_packet_life_time(), c.get_max_retransmits()])

# ─── Offerer ──────────────────────────────────────────────────────────────────

func _send_paced() -> void:
	var due := int((now() - send_started) * SEND_HZ)
	if due > COUNT:
		due = COUNT
	var payload := PackedByteArray()
	payload.resize(SIZE)
	for l in LABELS:
		var c: WebRTCDataChannel = chans[l]
		while sent[l] < due:
			payload.encode_u32(0, sent[l])
			# A refused send would skew the delivery ratio, so count it as not
			# sent rather than silently as a loss.
			if c.put_packet(payload) != OK:
				break
			sent[l] = int(sent[l]) + 1

func _everything_sent() -> bool:
	for l in LABELS:
		if int(sent[l]) < COUNT:
			return false
	return true

func _send_sentinel() -> void:
	# On the reliable channel only: it is the one guaranteed to arrive, so it
	# can mark the end of a stream whose other channels are lossy by design.
	var p := PackedByteArray()
	p.resize(SIZE)
	p.encode_u32(0, SENTINEL)
	if chans["reliable"].put_packet(p) == OK:
		sentinel_sent = true
		out("sentinel sent at t=%.2f after a %.1fs grace" % [now(), GRACE_S])
		phase = "waiting"

func _verdict() -> void:
	var rel_delivered: int = int(report.get("reliable", -1))
	var rel_ok: bool = rel_delivered == COUNT
	out("delivery per channel (of %d sent each):" % COUNT)
	var lossy := 0
	for l in LABELS:
		var got: int = int(report.get(l, -1))
		var pct: float = 100.0 * float(got) / float(COUNT)
		out("  %-8s delivered=%d (%.1f%%) out_of_order=%d" % [
			l, got, pct, int(report.get(l + "_oo", -1))])
		if l != "reliable" and got < COUNT:
			lossy += 1
	out("reliable_lossless=%s unreliable_channels_that_lost=%d of %d" % [
		str(rel_ok), lossy, LABELS.size() - 1])

	var verdict := "BROKEN"
	if not rel_ok:
		# The control channel losing data means the link was saturated or
		# something is badly wrong; no conclusion about transfer modes is
		# available from this run.
		verdict = "CONTROL_LOST_DATA"
	elif lossy == 0:
		# Either the link had no loss (check --loss), or the unreliable
		# configuration is not taking effect -- the F8 class of bug.
		verdict = "MODES_INDISTINGUISHABLE"
	else:
		verdict = "MODES_DIFFER"
	out("loss_modes verdict=%s" % verdict)
	phase = "done"

# ─── Answerer ─────────────────────────────────────────────────────────────────

func _drain() -> void:
	for l in chans.keys():
		var c: WebRTCDataChannel = chans[l]
		while c != null and c.get_available_packet_count() > 0:
			var p := c.get_packet()
			if p.size() < 4:
				continue
			var idx := int(p.decode_u32(0))
			if idx == SENTINEL:
				_report()
				return
			if idx < int(last_idx[l]):
				out_of_order[l] = int(out_of_order[l]) + 1
			last_idx[l] = idx
			recv[l] = int(recv[l]) + 1

func _report() -> void:
	if reported:
		return
	reported = true
	var msg := {"type": "report"}
	for l in LABELS:
		msg[l] = recv[l]
		msg[l + "_oo"] = out_of_order[l]
		out("got %s delivered=%d out_of_order=%d" % [l, int(recv[l]), int(out_of_order[l])])
	_send_signal(msg)
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
	if not _all_open():
		if phase == "connecting" and now() > 60.0:
			out("TIMEOUT only %d of %d channels open" % [chans.size(), LABELS.size()])
			phase = "done"
		return

	if role != "offerer":
		if phase == "connecting":
			out("CONNECTED at t=%.2fs, all %d channels open" % [now(), chans.size()])
			_report_config()
			phase = "receiving"
		_drain()
		if phase == "reported" and now() - reported_at > 2.0:
			phase = "done"
		return

	match phase:
		"connecting":
			out("CONNECTED at t=%.2fs; sending %d x %d B per channel at %.0f Hz" % [
				now(), COUNT, SIZE, SEND_HZ])
			_report_config()
			for c in chans.values():
				c.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
			send_started = now()
			phase = "sending"

		"sending":
			_send_paced()
			if _everything_sent():
				all_sent_at = now()
				out("all channels sent %d packets by t=%.2f" % [COUNT, all_sent_at])
				phase = "grace"

		"grace":
			if now() - all_sent_at > GRACE_S:
				_send_sentinel()

		"waiting":
			if now() > budget_s() - 20.0:
				out("TIMEOUT waiting for the peer's report")
				_verdict()
