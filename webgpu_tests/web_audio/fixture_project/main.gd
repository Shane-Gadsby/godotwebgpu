# Audio checks that run the same natively and on web, where AudioStreamPlayers default to Sample playback
# (Web Audio) while native always mixes through AudioServer (Stream).
#
# Each measurement prints "AUDIOTEST <name> <value>" (run_e2e.mjs compares those with a native run), and the
# suite ends with "[AUDIO-CHECK] PASS" or "[AUDIO-CHECK] FAIL: <checks>", which the scene smoketest requires.
# Numbers are checked against what native playback does by definition. They are only reported under the
# dummy audio driver (headless native), whose positions wander by ~100 ms between runs.
extends Node3D

const RATE := 22050 # Not the mix rate: exercises resampling and the loop-point rescale.
# Seconds. Finished is signaled on the main loop after the end; positions are read there too.
const FINISH_TOLERANCE := 0.05
const POSITION_TOLERANCE := 0.03

var failures := PackedStringArray()
# The longest frame of the last _finished() wait: finished is only seen on a frame, so a finish time can be late
# by one. GitHub's runners render in software at a few frames per second, which failed mp3_finish by 12 ms.
var finish_frame_slack := 0.0
var timing_checks := AudioServer.get_driver_name() != "Dummy"


func _log(name: String, value) -> void:
	print("AUDIOTEST %s %s" % [name, str(value)])


func _check(name: String, ok: bool) -> void:
	_log(name, ok)
	if not ok:
		failures.push_back(name)


# A finish time from _finish_time(), allowed one frame of lateness on top of `tolerance`.
func _check_finish(name: String, got: float, expected: float, tolerance := FINISH_TOLERANCE) -> void:
	_check_near(name, got, expected, tolerance + finish_frame_slack)


func _check_near(name: String, got: float, expected: float, tolerance: float) -> void:
	_log(name, got)
	if timing_checks and absf(got - expected) > tolerance:
		failures.push_back("%s (%.3f, expected %.3f)" % [name, got, expected])


func _wav(seconds: float, loop_mode := AudioStreamWAV.LOOP_DISABLED, loop_begin := 0.0, loop_end := 0.0) -> AudioStreamWAV:
	var frames := int(seconds * RATE)
	var data := PackedByteArray()
	data.resize(frames * 2)
	for i in frames:
		data.encode_s16(i * 2, int(sin(i * 0.05) * 8000.0))
	var wav := AudioStreamWAV.new()
	wav.format = AudioStreamWAV.FORMAT_16_BITS
	wav.mix_rate = RATE
	wav.stereo = false
	wav.data = data
	wav.loop_mode = loop_mode
	wav.loop_begin = int(loop_begin * RATE)
	wav.loop_end = int(loop_end * RATE)
	return wav


func _player(stream: AudioStream, pitch := 1.0, bus := &"Master", type := AudioServer.PLAYBACK_TYPE_DEFAULT) -> AudioStreamPlayer:
	var p := AudioStreamPlayer.new()
	p.stream = stream
	p.pitch_scale = pitch
	p.bus = bus
	p.playback_type = type
	add_child(p)
	return p


func _wait(seconds: float) -> void:
	await get_tree().create_timer(seconds).timeout


# Waits for `p` to emit `finished`, for `limit` seconds at most, and returns when it did (Time.get_ticks_usec()).
# A sound that never finishes fails `<name>_finished` and returns 0, rather than hanging the suite until the
# smoketest gives up on it without saying where it stopped.
func _finished(p: Node, name: String, limit := 5.0) -> int:
	var state := {"at": 0}
	var on_finished := func() -> void: state.at = Time.get_ticks_usec()
	p.finished.connect(on_finished, CONNECT_ONE_SHOT)
	var deadline := Time.get_ticks_msec() + int(limit * 1000.0)
	finish_frame_slack = 0.0
	var frame_start := Time.get_ticks_usec()
	while state.at == 0 and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
		var now := Time.get_ticks_usec()
		finish_frame_slack = maxf(finish_frame_slack, (now - frame_start) / 1000000.0)
		frame_start = now
	if state.at == 0:
		p.finished.disconnect(on_finished)
		_check("%s_finished" % name, false)
	return state.at


# `direction` is -1 when the position runs backward at that point (ping-pong return, backward loop).
func _position_after(stream: AudioStream, seconds: float, pitch := 1.0, from := 0.0, direction := 1.0, type := AudioServer.PLAYBACK_TYPE_DEFAULT) -> float:
	var p := _player(stream, pitch, &"Master", type)
	p.play(from)
	var start := Time.get_ticks_usec()
	await _wait(seconds)
	# Correct for timer overshoot: report the position at exactly `seconds`.
	var late := (Time.get_ticks_usec() - start) / 1000000.0 - seconds
	var pos := p.get_playback_position()
	p.queue_free()
	return snappedf(pos - late * pitch * direction, 0.001)


func _finish_time(stream: AudioStream, pitch := 1.0, type := AudioServer.PLAYBACK_TYPE_DEFAULT) -> float:
	var p := _player(stream, pitch, &"Master", type)
	var start := Time.get_ticks_usec()
	p.play()
	var at := await _finished(p, "finish_time")
	var elapsed := (at - start) / 1000000.0 if at else -1.0
	p.queue_free()
	return snappedf(elapsed, 0.001)


# A browser keeps audio suspended until the page gets a user gesture; nothing below means anything before.
func _wait_for_audio_clock() -> bool:
	var probe := _player(_wav(1.0, AudioStreamWAV.LOOP_FORWARD, 0.0, 1.0))
	probe.volume_db = -80.0
	probe.play()
	var deadline := Time.get_ticks_msec() + 30000
	while probe.get_playback_position() < 0.05 and Time.get_ticks_msec() < deadline:
		await _wait(0.1)
	var running := probe.get_playback_position() >= 0.05
	probe.queue_free()
	return running


func _bus_layout() -> void:
	# A layout whose bus 1 sends to bus 2, which only exists further down the list.
	var layout := AudioBusLayout.new()
	layout.set("bus/0/name", "Master")
	layout.set("bus/0/volume_db", 0.0)
	layout.set("bus/1/name", "Sfx")
	layout.set("bus/1/send", "Group")
	layout.set("bus/1/volume_db", -3.0)
	layout.set("bus/2/name", "Group")
	layout.set("bus/2/send", "Master")
	layout.set("bus/2/volume_db", -3.0)
	layout.set("bus/3/name", "Capture")
	layout.set("bus/3/send", "Master")
	layout.set("bus/3/effect/0/effect", AudioEffectCapture.new())
	layout.set("bus/3/effect/0/enabled", true)
	layout.set("bus/4/name", "Fx")
	layout.set("bus/4/send", "Master")
	layout.set("bus/4/effect/0/effect", AudioEffectReverb.new())
	layout.set("bus/4/effect/0/enabled", true)
	AudioServer.set_bus_layout(layout)
	_check("bus_count", AudioServer.bus_count == 5)
	_check("sfx_sends_to_later_bus", AudioServer.get_bus_send(AudioServer.get_bus_index(&"Sfx")) == &"Group")


func _sample_playback() -> void:
	var plain := _wav(0.4)
	var looped := _wav(0.4, AudioStreamWAV.LOOP_FORWARD, 0.1, 0.3)
	var pingpong := _wav(0.4, AudioStreamWAV.LOOP_PINGPONG, 0.1, 0.3)
	var backward := _wav(0.4, AudioStreamWAV.LOOP_BACKWARD, 0.1, 0.3)

	_check_finish("finish_pitch_1", await _finish_time(plain), 0.4)
	_check_finish("finish_pitch_2", await _finish_time(plain, 2.0), 0.2)
	_check_near("position_plain_0.25", await _position_after(plain, 0.25), 0.25, POSITION_TOLERANCE)
	_check_near("position_seek_0.2_plus_0.1", await _position_after(plain, 0.1, 1.0, 0.2), 0.3, POSITION_TOLERANCE)
	# Loops between 0.1 and 0.3 s.
	_check_near("position_forward_0.45", await _position_after(looped, 0.45), 0.25, POSITION_TOLERANCE)
	_check_near("position_forward_pitch_2_0.3", await _position_after(looped, 0.3, 2.0), 0.2, POSITION_TOLERANCE)
	_check_near("position_pingpong_0.45", await _position_after(pingpong, 0.45, 1.0, 0.0, -1.0), 0.15, POSITION_TOLERANCE)
	_check_near("position_backward_seek_0.25_plus_0.1", await _position_after(backward, 0.1, 1.0, 0.25, -1.0), 0.15, POSITION_TOLERANCE)

	# seek() while playing, and a pitch change mid-play.
	var p := _player(plain)
	p.play()
	await _wait(0.05)
	p.seek(0.3)
	var t0 := Time.get_ticks_usec()
	await _wait(0.05)
	var late := (Time.get_ticks_usec() - t0) / 1000000.0 - 0.05
	_check_near("seek_0.3_plus_0.05", snappedf(p.get_playback_position() - late, 0.001), 0.35, POSITION_TOLERANCE)
	p.queue_free()

	# Both waits are corrected for overshoot, the first at pitch 1, the second at pitch 2. Not on a loop, so a late
	# timer cannot wrap the position.
	p = _player(_wav(2.0))
	t0 = Time.get_ticks_usec()
	p.play()
	await _wait(0.1)
	var late_before := (Time.get_ticks_usec() - t0) / 1000000.0 - 0.1
	p.pitch_scale = 2.0
	t0 = Time.get_ticks_usec()
	await _wait(0.05)
	late = (Time.get_ticks_usec() - t0) / 1000000.0 - 0.05
	_check_near("pitch_change_mid_play", snappedf(p.get_playback_position() - late_before - late * 2.0, 0.001), 0.2, POSITION_TOLERANCE)
	p.queue_free()

	# Pause holds the position. Not on a loop: a runner's late timers carried the resumed position past the loop
	# end and back below where it was paused, which failed resumes_playing on audio that had resumed.
	p = _player(_wav(2.0))
	p.play()
	await _wait(0.15)
	p.stream_paused = true
	var held := p.get_playback_position()
	await _wait(0.2)
	_check("pause_holds", absf(p.get_playback_position() - held) < 0.03)
	p.stream_paused = false
	await _wait(0.1)
	_check("resumes_playing", p.playing and p.get_playback_position() > held)
	p.stop()
	_check("stopped", not p.playing)
	p.queue_free()

	# Several voices of one player.
	p = _player(plain)
	p.max_polyphony = 3
	p.play()
	await _wait(0.05)
	p.play()
	await _wait(0.05)
	p.play()
	_check("polyphony_playing", p.playing)
	await _finished(p, "polyphony")
	await _wait(0.15)
	_check("polyphony_all_finished", not p.playing)
	p.queue_free()


func _compressed_samples() -> void:
	# Ogg Vorbis and MP3 at 44.1 kHz, looping back to loop_offset at their end.
	for path: String in ["res://tone.ogg", "res://tone.mp3"]:
		var stream: AudioStream = load(path)
		var kind := path.get_extension()
		stream.set("loop", true)
		stream.set("loop_offset", 0.1)
		var length := stream.get_length()
		_check("%s_length" % kind, absf(length - 0.5) < 0.06)
		var at := length + 0.1
		_check_near("%s_loop_position" % kind, await _position_after(stream, at), 0.1 + (at - length), POSITION_TOLERANCE)
		stream.set("loop", false)
		_check_finish("%s_finish" % kind, await _finish_time(stream), length)


func _bus_edits() -> void:
	# A bus that sends to a later one, and runtime bus edits while a sample plays on it.
	var looped := _wav(0.4, AudioStreamWAV.LOOP_FORWARD, 0.1, 0.3)
	var on_bus := _player(looped, 1.0, &"Sfx")
	on_bus.play()
	await _wait(0.1)
	AudioServer.add_bus(1)
	AudioServer.move_bus(1, 3)
	AudioServer.set_bus_solo(AudioServer.get_bus_index(&"Sfx"), true)
	AudioServer.set_bus_mute(AudioServer.get_bus_index(&"Group"), true)
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index(&"Sfx"), -6.0)
	await _wait(0.1)
	AudioServer.set_bus_solo(AudioServer.get_bus_index(&"Sfx"), false)
	AudioServer.set_bus_mute(AudioServer.get_bus_index(&"Group"), false)
	_check("bus_edits_still_playing", on_bus.playing)
	on_bus.queue_free()
	AudioServer.remove_bus(AudioServer.get_bus_index(&"New Bus"))
	_check("bus_removed", AudioServer.get_bus_index(&"New Bus") == -1)


func _spatial_and_polyphonic() -> void:
	var looped := _wav(0.4, AudioStreamWAV.LOOP_FORWARD, 0.1, 0.3)
	var plain := _wav(0.4)

	# 3D: moving player, attenuation filter, doppler pitch.
	var p3d := AudioStreamPlayer3D.new()
	p3d.stream = looped
	p3d.attenuation_filter_cutoff_hz = 2000.0
	p3d.doppler_tracking = AudioStreamPlayer3D.DOPPLER_TRACKING_PHYSICS_STEP
	add_child(p3d)
	p3d.position = Vector3(0, 0, -5)
	p3d.play()
	for i in 20:
		p3d.position.x = sin(i * 0.3) * 10.0
		await get_tree().physics_frame
	_check("player_3d_playing", p3d.playing)
	p3d.queue_free()

	# 2D, panned across the screen.
	var p2d := AudioStreamPlayer2D.new()
	p2d.stream = plain
	add_child(p2d)
	p2d.play()
	await _wait(0.05)
	_check("player_2d_playing", p2d.playing)
	await _finished(p2d, "player_2d")
	_check("player_2d_finished", not p2d.playing)
	p2d.queue_free()

	# Polyphonic, sample or stream as the platform default decides.
	var poly := AudioStreamPolyphonic.new()
	var pp := _player(poly)
	pp.play()
	var playback := pp.get_stream_playback() as AudioStreamPlaybackPolyphonic
	var id := playback.play_stream(plain, 0.0, 0.0, 2.0)
	await _wait(0.1)
	_check("polyphonic_playing", playback.is_stream_playing(id))
	await _wait(0.3)
	_check("polyphonic_pitch_2_done", not playback.is_stream_playing(id))
	pp.queue_free()


func _stream_playback() -> void:
	# Stream playback goes through AudioServer's mixer on every platform, web included.
	var plain := _wav(0.4)
	var looped := _wav(0.4, AudioStreamWAV.LOOP_FORWARD, 0.1, 0.3)
	var stream_type := AudioServer.PLAYBACK_TYPE_STREAM
	_check_finish("stream_finish", await _finish_time(plain, 1.0, stream_type), 0.4, FINISH_TOLERANCE + 0.03)
	_check_near("stream_position_forward_0.45", await _position_after(looped, 0.45, 1.0, 0.0, 1.0, stream_type), 0.25, POSITION_TOLERANCE + 0.03)

	# A bus effect hears it: AudioEffectCapture fills up and the bus meter moves.
	var capture_bus := AudioServer.get_bus_index(&"Capture")
	var capture := AudioServer.get_bus_effect(capture_bus, 0) as AudioEffectCapture
	capture.clear_buffer()
	var p := _player(looped, 1.0, &"Capture", stream_type)
	p.play()
	await _wait(0.3)
	_check("capture_effect_receives_frames", capture.get_frames_available() > 0)
	var frames := capture.get_buffer(capture.get_frames_available())
	var peak := 0.0
	for f in frames:
		peak = maxf(peak, absf(f.x))
	_check("capture_effect_hears_audio", peak > 0.05)
	_check("bus_peak_meter_moves", AudioServer.get_bus_peak_volume_left_db(capture_bus, 0) > -100.0)
	p.queue_free()

	# A generator: the mixer consumes what the script pushes.
	var generator := AudioStreamGenerator.new()
	generator.mix_rate = 44100
	generator.buffer_length = 0.5
	var gp := _player(generator)
	gp.play()
	var gen := gp.get_stream_playback() as AudioStreamGeneratorPlayback
	var pushed := 0
	for i in 8:
		while gen.can_push_buffer(512):
			var chunk := PackedVector2Array()
			chunk.resize(512)
			chunk.fill(Vector2(0.1, 0.1))
			gen.push_buffer(chunk)
			pushed += 512
		await _wait(0.05)
	_check("generator_consumed", pushed > int(generator.buffer_length * generator.mix_rate) + 2048)
	_check("generator_no_skips", gen.get_skips() == 0)
	gp.queue_free()

	# A randomizer (a meta stream: always mixed by AudioServer) finishes like its pick.
	var randomizer := AudioStreamRandomizer.new()
	randomizer.add_stream(-1, plain)
	randomizer.add_stream(-1, _wav(0.4))
	_check_finish("randomizer_finish", await _finish_time(randomizer), 0.4, FINISH_TOLERANCE + 0.03)


# What Sample playback cannot do is warned about, once: run_e2e.mjs expects each of these warnings exactly once
# on web (where these players use Sample playback) and none natively (where they use Stream).
func _limitation_warnings() -> void:
	var fx := _player(_wav(0.1), 1.0, &"Fx") # A reverb on the bus: bypassed by Sample playback.
	fx.play()
	await _finished(fx, "fx_bus")
	fx.play()
	await _finished(fx, "fx_bus_again")
	fx.queue_free()
	var long := _player(_wav(31.0)) # Decoded whole as a Sample.
	long.volume_db = -80.0
	long.play()
	await _wait(0.1)
	long.stop()
	long.queue_free()
	_check("limitation_warnings_exercised", true)


func _ready() -> void:
	_check("audio_clock_running", await _wait_for_audio_clock())
	if failures.is_empty():
		_bus_layout()
		await _sample_playback()
		await _compressed_samples()
		await _bus_edits()
		await _spatial_and_polyphonic()
		await _stream_playback()
		await _limitation_warnings()

	if failures.is_empty():
		print("[AUDIO-CHECK] PASS")
	else:
		print("[AUDIO-CHECK] FAIL: %s" % ", ".join(failures))
	_log("done", true)
	if OS.has_feature("web"):
		return # Stay on the page; the runner closes it.
	await get_tree().process_frame # Let the queue_free()s above happen before quitting.
	get_tree().quit()
