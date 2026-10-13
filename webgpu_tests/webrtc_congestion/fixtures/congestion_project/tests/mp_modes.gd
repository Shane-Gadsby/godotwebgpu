# Sends through WebRTCMultiplayerPeer's own transfer modes over a lossy link,
# and counts what arrives for each.
#
# This exists because `loss_modes.gd` CANNOT verify a change to the shipped
# default. That fixture builds its channels with explicit configuration
# dictionaries, so it measures the mechanism (a 1 ms lifetime versus a 50 ms
# one) and is unaffected by what `WebRTCMultiplayerPeer::add_peer` defaults to.
# This one never names a lifetime at all: it calls `add_peer()` with two
# arguments and lets the engine choose, so whatever delivery it measures is what
# a real project gets.
#
# Keep both. loss_modes pins the mechanism and would catch a regression in the
# transport; this pins the default and would catch someone lowering it again.
#
#   ./shape.sh --rate 4mbit --delay 25ms --loss 5% -- \
#       env RTC_TEST=mp_modes node run_congestion.mjs
#
# F10 and the default change: webgpu_notes/TASKS.md §4.20
#
# Both peers build a two-peer mesh, as the parity tier's `mesh` fixture does,
# but across two processes and a shaped link. The offerer sends a paced stream
# in each of the three transfer modes in turn; the answerer counts per mode and
# reports back. Paced well under the shaped rate so the loss is netem's and not
# a queue this fixture overfilled, with RELIABLE as the control.
extends "res://tests/cong_base.gd"

const SIZE := 1000
const COUNT := 200
const SEND_HZ := 20.0
const GRACE_S := 4.0
const SENTINEL := 0xFFFFFFFF

# Index matches the order sent, so the answerer can attribute by channel.
const MODES := [
	MultiplayerPeer.TRANSFER_MODE_RELIABLE,
	MultiplayerPeer.TRANSFER_MODE_UNRELIABLE_ORDERED,
	MultiplayerPeer.TRANSFER_MODE_UNRELIABLE,
]
const MODE_NAMES := ["RELIABLE", "UNRELIABLE_ORDERED", "UNRELIABLE"]

var mp: WebRTCMultiplayerPeer
var sent := {}
var recv := {}
var mode_idx := 0
var send_started := -1.0
var all_sent_at := -1.0
var report := {}
var reported := false
var reported_at := -1.0
var connected_seen := false

func budget_s() -> float:
	return 240.0

func _init() -> void:
	for n in MODE_NAMES:
		sent[n] = 0
		recv[n] = 0

# The mesh is driven through WebRTCMultiplayerPeer, so this fixture owns the
# WebRTCPeerConnection rather than letting the base class poll it directly:
# add_peer() takes it over, and polling it twice is not safe.
func _ready() -> void:
	super._ready()
	mp = WebRTCMultiplayerPeer.new()
	var self_id := 1 if role == "offerer" else 2
	var other_id := 2 if role == "offerer" else 1
	out("create_mesh(%d)=%d" % [self_id, mp.create_mesh(self_id)])
	# TWO arguments on purpose: the third is unreliable_lifetime, and the point
	# of this fixture is what the engine picks when a project does not say.
	out("add_peer(%d)=%d  [lifetime left to the engine default]" % [
		other_id, mp.add_peer(pc, other_id)])

func _channels() -> Array:
	return []

func _step() -> void:
	# WebRTCMultiplayerPeer polls the connection and its channels itself.
	mp.poll()

	if not connected_seen:
		if mp.get_available_packet_count() >= 0 and mp.get_peers().size() > 0 \
				and mp.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED:
			# Gate on a peer being present, not on the status alone: the status
			# reads CONNECTED as soon as create_mesh() succeeds, before any
			# channel is open (the parity tier's rule 4).
			connected_seen = true
			out("MESH CONNECTED at t=%.2fs peers=%s" % [now(), str(mp.get_peers())])
			send_started = now()
			phase = "sending" if role == "offerer" else "receiving"
		elif now() > 60.0:
			out("TIMEOUT mesh never connected, status=%d peers=%s" % [
				mp.get_connection_status(), str(mp.get_peers())])
			phase = "done"
		return

	if role != "offerer":
		_drain()
		if phase == "reported" and now() - reported_at > 2.0:
			phase = "done"
		return

	match phase:
		"sending":
			_send_paced()
			if int(sent[MODE_NAMES[mode_idx]]) >= COUNT:
				out("%s: sent %d" % [MODE_NAMES[mode_idx], COUNT])
				mode_idx += 1
				send_started = now()
				if mode_idx >= MODES.size():
					all_sent_at = now()
					phase = "grace"
		"grace":
			if now() - all_sent_at > GRACE_S:
				_send_sentinel()
		"waiting":
			if now() > budget_s() - 20.0:
				out("TIMEOUT waiting for the peer's report")
				_verdict()

func _send_paced() -> void:
	var name: String = MODE_NAMES[mode_idx]
	var due := int((now() - send_started) * SEND_HZ)
	if due > COUNT:
		due = COUNT
	mp.set_transfer_mode(MODES[mode_idx])
	mp.set_target_peer(2)
	var payload := PackedByteArray()
	payload.resize(SIZE)
	while int(sent[name]) < due:
		payload.encode_u32(0, mode_idx)
		payload.encode_u32(4, int(sent[name]))
		if mp.put_packet(payload) != OK:
			break
		sent[name] = int(sent[name]) + 1

func _send_sentinel() -> void:
	mp.set_transfer_mode(MultiplayerPeer.TRANSFER_MODE_RELIABLE)
	mp.set_target_peer(2)
	var p := PackedByteArray()
	p.resize(SIZE)
	p.encode_u32(0, SENTINEL)
	if mp.put_packet(p) == OK:
		out("sentinel sent at t=%.2f" % now())
		phase = "waiting"

func _drain() -> void:
	while mp.get_available_packet_count() > 0:
		var p := mp.get_packet()
		if p.size() < 8:
			continue
		var tag := p.decode_u32(0)
		if tag == SENTINEL:
			_report()
			return
		if int(tag) < MODE_NAMES.size():
			var n: String = MODE_NAMES[int(tag)]
			recv[n] = int(recv[n]) + 1

func _report() -> void:
	if reported:
		return
	reported = true
	var msg := {"type": "report"}
	for n in MODE_NAMES:
		msg[n] = recv[n]
		out("got %s delivered=%d" % [n, int(recv[n])])
	_send_signal(msg)
	reported_at = now()
	phase = "reported"

func _on_signal(msg: Dictionary) -> void:
	if str(msg.get("type", "")) == "report":
		report = msg
		_verdict()
		return
	super._on_signal(msg)

func _verdict() -> void:
	var rel: int = int(report.get("RELIABLE", -1))
	var rel_ok: bool = rel == COUNT
	out("delivery per transfer mode (of %d sent each):" % COUNT)
	var worst := 100.0
	for n in MODE_NAMES:
		var got: int = int(report.get(n, -1))
		var pct := 100.0 * float(got) / float(COUNT)
		out("  %-20s delivered=%d (%.1f%%)" % [n, got, pct])
		if n != "RELIABLE" and pct < worst:
			worst = pct
	out("reliable_lossless=%s worst_unreliable=%.1f%%" % [str(rel_ok), worst])

	var verdict := "BROKEN"
	if not rel_ok:
		verdict = "CONTROL_LOST_DATA"
	elif worst >= 99.9:
		# Nothing was lost anywhere, so the link had no loss to speak of and
		# this run says nothing about the default. Check --loss.
		verdict = "NO_PRESSURE"
	elif worst >= 80.0:
		# The unreliable modes lose roughly what the link dropped, which is what
		# "unreliable" is supposed to mean and what the raised default buys.
		verdict = "DEFAULT_SANE"
	else:
		# Losing far more than the link dropped is F10: a lifetime shorter than
		# the round trip means nothing lost is ever retransmitted.
		verdict = "DEFAULT_TOO_SHORT"
	out("mp_modes verdict=%s" % verdict)
	phase = "done"
