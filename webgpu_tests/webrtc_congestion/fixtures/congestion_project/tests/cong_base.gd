# Shared base for every congestion fixture. Unlike the parity tier's fixtures
# (webgpu_tests/webrtc_parity), these run as TWO PROCESSES — usually on two
# sides of a shaped link — so they cannot shuttle SDP/ICE directly in GDScript
# and need real signaling.
#
# This base owns all of that: a WebSocketPeer client to signal_server.mjs, the
# offer/answer/ICE exchange, and the per-tick poll. A fixture subclasses it and
# implements _on_open() and _step(); everything above the data channel is done.
#
# Roles. "offerer" creates the data channel(s) and calls create_offer();
# "answerer" waits for data_channel_received. Both run the same fixture script,
# and which one a process is comes from the command line (`-- <test> <role>`) or
# the URL (`?test=<name>&role=<role>`). Only the offerer prints a verdict, so
# one run produces one verdict and the answerer's lines are prefixed to keep the
# two streams separable.
#
# Rules carried over from the parity tier, both still load-bearing:
#   1. Never block the main loop. Drive from _process(). On web the JS bridge is
#      callback-driven from the browser event loop, so a blocking loop starves
#      every ICE/onmessage callback and nothing ever connects.
#   2. Print everything as `RTC| key=value`, stable across runs.
#
# One rule is new, and it is the whole reason this tier exists: a congested link
# makes everything slower by design, so **no fixture here may use a tick count
# as a timeout**. Ticks are frames, and frames are not time. Use elapsed
# seconds (`now()`), and size every budget off the shaped rate.
extends Node

var phase := "start"
var ticks := 0
var role := ""
var test_name := ""
var started_ms := 0

var ws: WebSocketPeer
var pc: WebRTCPeerConnection
var signal_url := "ws://127.0.0.1:9099"
var ws_connected := false
var _finished := false
var _pending_out := []

# ─── Output ───────────────────────────────────────────────────────────────────

func out(s: String) -> void:
	# The answerer's output is tagged so one combined log stays readable, and so
	# a verdict can never be attributed to the wrong side.
	var line := "RTC| " + ("" if role == "offerer" else "[B] ") + s
	print(line)
	if OS.has_feature("web"):
		JavaScriptBridge.eval(
			"(window.__rtc_lines=window.__rtc_lines||[]).push(%s);" % JSON.stringify(line), true)

func finish() -> void:
	# Idempotent: a fixture can reach "done" from _on_signal (a verdict computed
	# when the peer's report lands) or from _step, and both paths funnel through
	# _process's tail. Without the guard the END line could print twice.
	if _finished:
		return
	_finished = true
	phase = "done"
	out("END")
	if ws and ws.get_ready_state() == WebSocketPeer.STATE_OPEN:
		ws.send_text(JSON.stringify({"type": "bye"}))
		# One poll() does not get the frame onto the wire, and quitting right
		# after it drops the bye -- which left the other process sitting out its
		# whole budget the first time this ran. Flush briefly before quitting.
		# Blocking is fine HERE and nowhere else in this tier: measurement is
		# over, so there is no callback left to starve.
		if not OS.has_feature("web"):
			for _i in range(20):
				ws.poll()
				if ws.get_ready_state() != WebSocketPeer.STATE_OPEN:
					break
				OS.delay_msec(10)
	if OS.has_feature("web"):
		JavaScriptBridge.eval("window.__rtc_done = true;", true)
	else:
		get_tree().quit()

# Seconds since _ready(). Wall clock, not ticks — see the header.
func now() -> float:
	return float(Time.get_ticks_msec() - started_ms) / 1000.0

# ─── Setup ────────────────────────────────────────────────────────────────────

func setup(p_role: String, p_test: String, p_url: String) -> void:
	role = p_role
	test_name = p_test
	signal_url = p_url

func _ready() -> void:
	started_ms = Time.get_ticks_msec()
	out("env web=%s threads=%s role=%s test=%s" % [
		str(OS.has_feature("web")), str(OS.has_feature("threads")), role, test_name])
	pc = WebRTCPeerConnection.new()
	out("impl=%s" % pc.get_class())
	# No STUN/TURN: both peers are on a link we built, and a public STUN server
	# would be unreachable inside the test namespace anyway. Host candidates are
	# all that is needed and all that should be used.
	pc.initialize({"iceServers": []})
	pc.session_description_created.connect(_on_sdp)
	pc.ice_candidate_created.connect(_on_ice)
	pc.data_channel_received.connect(_on_data_channel)

	ws = WebSocketPeer.new()
	var err := ws.connect_to_url(signal_url)
	if err != OK:
		out("FATAL cannot reach signaling at %s (err=%d)" % [signal_url, err])
		finish()
		return
	out("signaling connecting to %s" % signal_url)
	phase = "signaling"

# ─── Signaling ────────────────────────────────────────────────────────────────

func _send_signal(msg: Dictionary) -> void:
	if ws_connected:
		ws.send_text(JSON.stringify(msg))
	else:
		# ICE candidates are generated as soon as the offer is set, which can be
		# before the socket finishes opening. Dropping them silently is how a
		# connection ends up never establishing, so queue instead.
		_pending_out.append(msg)

func _flush_signals() -> void:
	for m in _pending_out:
		ws.send_text(JSON.stringify(m))
	_pending_out.clear()

func _on_sdp(type: String, sdp: String) -> void:
	pc.set_local_description(type, sdp)
	_send_signal({"type": type, "sdp": sdp})

func _on_ice(media: String, index: int, name: String) -> void:
	_send_signal({"type": "ice", "media": media, "index": index, "name": name})

func _on_signal(msg: Dictionary) -> void:
	match str(msg.get("type", "")):
		"ready":
			# Both peers are present. Only now can the offer be made: an offer
			# sent before the answerer joined would be relayed to nobody.
			if role == "offerer" and phase == "signaling":
				out("peers ready at t=%.2fs, creating offer" % now())
				phase = "connecting"
				_create_channels()
				pc.create_offer()
			elif phase == "signaling":
				phase = "connecting"
		"offer", "answer":
			pc.set_remote_description(str(msg["type"]), str(msg["sdp"]))
		"ice":
			pc.add_ice_candidate(str(msg["media"]), int(msg["index"]), str(msg["name"]))
		"bye":
			if phase != "done":
				out("peer sent bye at t=%.2fs" % now())
				_on_peer_bye()

func _poll_signaling() -> void:
	ws.poll()
	var state := ws.get_ready_state()
	if state == WebSocketPeer.STATE_OPEN:
		if not ws_connected:
			ws_connected = true
			out("signaling open at t=%.2fs" % now())
			ws.send_text(JSON.stringify({"type": "hello", "role": role}))
			_flush_signals()
		while ws.get_available_packet_count() > 0:
			var raw := ws.get_packet().get_string_from_utf8()
			var parsed = JSON.parse_string(raw)
			if parsed is Dictionary:
				_on_signal(parsed)
	elif state == WebSocketPeer.STATE_CLOSED and ws_connected and phase != "done":
		out("FATAL signaling closed unexpectedly (code=%d)" % ws.get_close_code())
		finish()

# ─── Hooks for fixtures ───────────────────────────────────────────────────────

# Offerer only: create the data channel(s). Called once both peers are present.
func _create_channels() -> void:
	pass

# Answerer only: a channel arrived.
func _on_data_channel(_ch: WebRTCDataChannel) -> void:
	pass

# The peer finished and said so. Default is to finish too, which is what an
# answerer normally wants.
func _on_peer_bye() -> void:
	if role != "offerer":
		finish()

# Called every tick once past signaling. Fixtures implement this.
func _step() -> void:
	pass

# Channels a fixture wants polled every tick. Override to return them.
func _channels() -> Array:
	return []

# Hard ceiling in seconds. A congested run is slow on purpose, so this is
# generous and exists only to stop a hung run from hanging CI.
func budget_s() -> float:
	return 180.0

func _process(_delta: float) -> void:
	if _finished:
		return
	ticks += 1
	_poll_signaling()
	# Everything below is skipped once something has set phase to "done" --
	# including a verdict computed inside _poll_signaling -- but the finish() at
	# the tail must still run. Returning early here instead is what left the
	# offerer alive with its verdict printed and no END line, so the runner sat
	# out its whole timeout waiting for a peer that was already finished.
	if phase != "done":
		pc.poll()
		for ch in _channels():
			if ch:
				ch.poll()
		if phase != "signaling":
			_step()
		if now() > budget_s():
			out("TIMEOUT phase=%s at t=%.2fs" % [phase, now()])
			phase = "done"
	if phase == "done":
		finish()
