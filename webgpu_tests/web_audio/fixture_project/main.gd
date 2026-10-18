# Runs the same audio checks natively (Stream playback) and on web (Sample playback) and prints one
# "AUDIOTEST <name> <value>" line per measurement, which run_e2e.mjs compares between the two.
extends Node3D

const RATE := 22050 # Not the mix rate: exercises resampling and the loop-point rescale.


func _log(name: String, value) -> void:
	print("AUDIOTEST %s %s" % [name, str(value)])


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


func _player(stream: AudioStream, pitch := 1.0, bus := &"Master") -> AudioStreamPlayer:
	var p := AudioStreamPlayer.new()
	p.stream = stream
	p.pitch_scale = pitch
	p.bus = bus
	add_child(p)
	return p


func _wait(seconds: float) -> void:
	await get_tree().create_timer(seconds).timeout


# `direction` is -1 when the position runs backward at that point (ping-pong return, backward loop).
func _position_after(stream: AudioStream, seconds: float, pitch := 1.0, from := 0.0, direction := 1.0) -> float:
	var p := _player(stream, pitch)
	p.play(from)
	var start := Time.get_ticks_usec()
	await _wait(seconds)
	# Correct for timer overshoot: report the position at exactly `seconds`.
	var late := (Time.get_ticks_usec() - start) / 1000000.0 - seconds
	var pos := p.get_playback_position()
	p.queue_free()
	return snappedf(pos - late * pitch * direction, 0.001)


func _finish_time(stream: AudioStream, pitch := 1.0) -> float:
	var p := _player(stream, pitch)
	var start := Time.get_ticks_usec()
	p.play()
	await p.finished
	var elapsed := (Time.get_ticks_usec() - start) / 1000000.0
	p.queue_free()
	return snappedf(elapsed, 0.001)


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
	AudioServer.set_bus_layout(layout)
	_log("bus_count", AudioServer.bus_count)
	_log("sfx_send", AudioServer.get_bus_send(AudioServer.get_bus_index(&"Sfx")))


func _ready() -> void:
	await _wait(0.5) # Let the audio device start.
	_bus_layout()

	var plain := _wav(0.4)
	var looped := _wav(0.4, AudioStreamWAV.LOOP_FORWARD, 0.1, 0.3)
	var pingpong := _wav(0.4, AudioStreamWAV.LOOP_PINGPONG, 0.1, 0.3)
	var backward := _wav(0.4, AudioStreamWAV.LOOP_BACKWARD, 0.1, 0.3)

	_log("finish_pitch_1", await _finish_time(plain))
	_log("finish_pitch_2", await _finish_time(plain, 2.0))
	_log("position_plain_0.25", await _position_after(plain, 0.25))
	_log("position_seek_0.2_plus_0.1", await _position_after(plain, 0.1, 1.0, 0.2))
	_log("position_forward_0.45", await _position_after(looped, 0.45))
	_log("position_forward_pitch_2_0.3", await _position_after(looped, 0.3, 2.0))
	_log("position_pingpong_0.45", await _position_after(pingpong, 0.45, 1.0, 0.0, -1.0))
	_log("position_backward_seek_0.25_plus_0.1", await _position_after(backward, 0.1, 1.0, 0.25, -1.0))

	# Pause holds the position.
	var p := _player(looped)
	p.play()
	await _wait(0.15)
	p.stream_paused = true
	var held := p.get_playback_position()
	await _wait(0.2)
	_log("pause_holds", absf(p.get_playback_position() - held) < 0.03)
	p.stream_paused = false
	await _wait(0.1)
	_log("resumes_playing", p.playing and p.get_playback_position() > held)
	p.stop()
	_log("stopped", not p.playing)
	p.queue_free()

	# A bus that sends to a later one, and runtime bus edits while a sample plays on it.
	var on_bus := _player(looped, 1.0, &"Sfx")
	on_bus.play()
	await _wait(0.1)
	AudioServer.add_bus(1)
	AudioServer.move_bus(1, 3)
	AudioServer.set_bus_solo(AudioServer.get_bus_index(&"Sfx"), true)
	AudioServer.set_bus_mute(AudioServer.get_bus_index(&"Group"), true)
	await _wait(0.1)
	AudioServer.set_bus_solo(AudioServer.get_bus_index(&"Sfx"), false)
	_log("bus_edits_still_playing", on_bus.playing)
	on_bus.queue_free()

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
	_log("player_3d_playing", p3d.playing)
	p3d.queue_free()

	# Polyphonic, sample or stream as the platform default decides.
	var poly := AudioStreamPolyphonic.new()
	var pp := _player(poly)
	pp.play()
	var playback := pp.get_stream_playback() as AudioStreamPlaybackPolyphonic
	var id := playback.play_stream(plain, 0.0, 0.0, 2.0)
	await _wait(0.1)
	_log("polyphonic_playing", playback.is_stream_playing(id))
	await _wait(0.3)
	_log("polyphonic_pitch_2_done", not playback.is_stream_playing(id))
	pp.queue_free()

	_log("done", true)
	await get_tree().process_frame # Let the queue_free()s above happen before quitting.
	get_tree().quit()
