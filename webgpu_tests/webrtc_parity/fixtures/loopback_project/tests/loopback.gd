# In-process WebRTC loopback: two peers in one process, SDP/ICE shuttled
# directly, no signaling server. Runs identically on native and in a web
# export, so the "RTC|" lines can be diffed between the two to produce the
# parity table. See webgpu_notes/webrtc_web.md.
extends "res://tests/rtc_test_base.gd"

var pc1: WebRTCPeerConnection
var pc2: WebRTCPeerConnection
var ch1: WebRTCDataChannel
var ch2: WebRTCDataChannel

var f2_close_tick := -1

const CH_CONFIG := { "ordered": true }
const BIN_PAYLOAD := "binary-round-trip"
const TXT_PAYLOAD := "text-round-trip"

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

	ch1 = pc1.create_data_channel("loopback", CH_CONFIG)
	out("ch1_created=%s class=%s" % [str(ch1 != null), ch1.get_class() if ch1 else "null"])
	out("pre_open ch1 %s" % _props(ch1))
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
	out("dc_received class=%s" % channel.get_class())

func _props(ch: WebRTCDataChannel) -> String:
	if ch == null:
		return "null"
	return "label=%s ready=%d ordered=%s id=%d lifetime=%d rtx=%d proto='%s' negotiated=%s buffered=%d wmode=%d" % [
		ch.get_label(), ch.get_ready_state(), str(ch.is_ordered()), ch.get_id(),
		ch.get_max_packet_life_time(), ch.get_max_retransmits(), ch.get_protocol(),
		str(ch.is_negotiated()), ch.get_buffered_amount(), ch.get_write_mode()]

func _step() -> void:
	match phase:
		"connecting":
			if ch1 and ch2 and ch1.get_ready_state() == WebRTCDataChannel.STATE_OPEN \
					and ch2.get_ready_state() == WebRTCDataChannel.STATE_OPEN:
				out("CONNECTED after %d ticks" % ticks)
				out("open ch1 %s" % _props(ch1))
				out("open ch2 %s" % _props(ch2))
				ch1.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
				ch1.put_packet(BIN_PAYLOAD.to_utf8_buffer())
				phase = "binary"

		"binary":
			if ch2.get_available_packet_count() > 0:
				var p = ch2.get_packet()
				out("binary rx='%s' ok=%s was_string=%s" % [
					p.get_string_from_utf8(),
					str(p.get_string_from_utf8() == BIN_PAYLOAD),
					str(ch2.was_string_packet())])
				ch1.set_write_mode(WebRTCDataChannel.WRITE_MODE_TEXT)
				ch1.put_packet(TXT_PAYLOAD.to_utf8_buffer())
				phase = "text"

		"text":
			if ch2.get_available_packet_count() > 0:
				var p = ch2.get_packet()
				out("text rx='%s' ok=%s was_string=%s" % [
					p.get_string_from_utf8(),
					str(p.get_string_from_utf8() == TXT_PAYLOAD),
					str(ch2.was_string_packet())])
				# F2: send, then close the sender WITHOUT ch2 draining first.
				ch1.set_write_mode(WebRTCDataChannel.WRITE_MODE_BINARY)
				ch1.put_packet("f2-pending".to_utf8_buffer())
				phase = "f2_wait_arrival"

		"f2_wait_arrival":
			# Wait for it to be queued on ch2, but do NOT read it.
			if ch2.get_available_packet_count() > 0:
				out("f2 queued_before_close=%d" % ch2.get_available_packet_count())
				ch1.close()
				f2_close_tick = ticks
				phase = "f2_wait_close"

		"f2_wait_close":
			if ch2.get_ready_state() == WebRTCDataChannel.STATE_CLOSED:
				out("f2 ch2_closed_after=%d ticks avail_after_close=%d" % [
					ticks - f2_close_tick, ch2.get_available_packet_count()])
				var err_before = ch2.get_available_packet_count()
				var got = ch2.get_packet()
				out("f2 drain_after_close len=%d content='%s' (expect native=drains, web=fails)" % [
					got.size(), got.get_string_from_utf8()])
				out("f2 verdict=%s" % ("DRAINED" if got.size() > 0 else "LOST"))
				phase = "done"
			elif ticks - f2_close_tick > 300:
				out("f2 ch2 never reached CLOSED (state=%d) avail=%d" % [
					ch2.get_ready_state(), ch2.get_available_packet_count()])
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
	if ticks >= 1500 and phase != "done":
		out("TIMEOUT in phase=%s conn1=%d conn2=%d ch1=%s ch2=%s" % [
			phase, pc1.get_connection_state(), pc2.get_connection_state(),
			ch1.get_ready_state() if ch1 else -1, ch2.get_ready_state() if ch2 else -1])
		phase = "done"
	if phase == "done":
		finish()
