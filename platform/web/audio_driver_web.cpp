/**************************************************************************/
/*  audio_driver_web.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "audio_driver_web.h"

#include "godot_audio.h"

#include "core/config/engine.h"
#include "core/math/math_funcs_binary.h"
#include "core/object/callable_mp.h"
#include "core/object/object.h"
#include "scene/resources/audio/audio_stream.h"
#include "scene/resources/audio/audio_stream_wav.h"
#include "servers/audio/audio_server.h"

#include <emscripten.h>

AudioDriverWeb::AudioContext AudioDriverWeb::audio_context;
AudioDriverWeb *AudioDriverWeb::singleton = nullptr;

bool AudioDriverWeb::is_available() {
	return godot_audio_is_available() != 0;
}

void AudioDriverWeb::_state_change_callback(int p_state) {
	AudioDriverWeb::audio_context.state = p_state;
}

void AudioDriverWeb::_latency_update_callback(float p_latency) {
	AudioDriverWeb::audio_context.output_latency = p_latency;
}

void AudioDriverWeb::_sample_playback_finished_callback(int p_playback_id) {
	if (!singleton) {
		return;
	}
	const ObjectID *playback_object = singleton->sample_playback_objects.getptr(p_playback_id);
	if (!playback_object) {
		return;
	}
	const ObjectID object_id = *playback_object;
	Ref<AudioSamplePlayback> playback = Object::cast_to<AudioSamplePlayback>(ObjectDB::get_instance(object_id));
	if (playback.is_valid()) {
		AudioServer::get_singleton()->stop_sample_playback(playback);
	}
	singleton->_forget_sample_playback(object_id); // In case AudioServer no longer listed it.
}

void AudioDriverWeb::_audio_driver_process(int p_from, int p_samples) {
	int32_t *stream_buffer = reinterpret_cast<int32_t *>(output_rb);
	const int max_samples = memarr_len(output_rb);

	int write_pos = p_from;
	int to_write = p_samples;
	if (to_write == 0) {
		to_write = max_samples;
	}
	// AudioServer writes samples as 32-bit integers (about 21 significant bits): convert them in place, at full
	// precision rather than shifted down to 16 bits.
	// High part
	if (write_pos + to_write > max_samples) {
		const int samples_high = max_samples - write_pos;
		audio_server_process(samples_high / channel_count, &stream_buffer[write_pos]);
		for (int i = write_pos; i < max_samples; i++) {
			output_rb[i] = float(stream_buffer[i]) * (1.0f / 2147483648.0f);
		}
		to_write -= samples_high;
		write_pos = 0;
	}
	// Leftover
	audio_server_process(to_write / channel_count, &stream_buffer[write_pos]);
	for (int i = write_pos; i < write_pos + to_write; i++) {
		output_rb[i] = float(stream_buffer[i]) * (1.0f / 2147483648.0f);
	}
}

void AudioDriverWeb::_audio_driver_capture(int p_from, int p_samples) {
	if (get_input_buffer().is_empty()) {
		return; // Input capture stopped.
	}
	const int max_samples = memarr_len(input_rb);

	int read_pos = p_from;
	int to_read = p_samples;
	if (to_read == 0) {
		to_read = max_samples;
	}
	// High part
	if (read_pos + to_read > max_samples) {
		const int samples_high = max_samples - read_pos;
		for (int i = read_pos; i < max_samples; i++) {
			input_buffer_write(int32_t(input_rb[i] * 32768.f) * (1U << 16));
		}
		to_read -= samples_high;
		read_pos = 0;
	}
	// Leftover
	for (int i = read_pos; i < read_pos + to_read; i++) {
		input_buffer_write(int32_t(input_rb[i] * 32768.f) * (1U << 16));
	}
}

Error AudioDriverWeb::init() {
	int latency = Engine::get_singleton()->get_audio_output_latency();
	if (!audio_context.inited) {
		audio_context.mix_rate = _get_configured_mix_rate();
		audio_context.channel_count = godot_audio_init(&audio_context.mix_rate, latency, &_state_change_callback, &_latency_update_callback);
		audio_context.inited = true;
	}
	mix_rate = audio_context.mix_rate;
	channel_count = audio_context.channel_count;
	buffer_length = Math::closest_power_of_2(uint32_t(latency * mix_rate / 1000));
	RETURN_IF_ERROR(create(buffer_length, channel_count));
	if (output_rb) {
		memdelete_arr(output_rb);
	}
	const size_t array_size = buffer_length * (size_t)channel_count;
	output_rb = memnew_arr(float, array_size);
	if (!output_rb) {
		return ERR_OUT_OF_MEMORY;
	}
	if (input_rb) {
		memdelete_arr(input_rb);
	}
	input_rb = memnew_arr(float, array_size);
	if (!input_rb) {
		return ERR_OUT_OF_MEMORY;
	}

	singleton = this;
	godot_audio_sample_set_finished_callback(&_sample_playback_finished_callback);

	return OK;
}

void AudioDriverWeb::start() {
	start(output_rb, memarr_len(output_rb), input_rb, memarr_len(input_rb));
}

void AudioDriverWeb::resume() {
	if (audio_context.state == 0) { // 'suspended'
		godot_audio_resume();
	}
}

float AudioDriverWeb::get_latency() {
	return audio_context.output_latency + (float(buffer_length) / mix_rate);
}

int AudioDriverWeb::get_mix_rate() const {
	return mix_rate;
}

AudioDriver::SpeakerMode AudioDriverWeb::get_speaker_mode() const {
	return get_speaker_mode_by_total_channels(channel_count);
}

void AudioDriverWeb::finish() {
	finish_driver();
	if (singleton == this) {
		singleton = nullptr;
	}
	if (output_rb) {
		memdelete_arr(output_rb);
		output_rb = nullptr;
	}
	if (input_rb) {
		memdelete_arr(input_rb);
		input_rb = nullptr;
	}
}

Error AudioDriverWeb::input_start() {
	lock();
	input_buffer_init(buffer_length);
	unlock();
	if (godot_audio_input_start()) {
		return FAILED;
	}
	return OK;
}

Error AudioDriverWeb::input_stop() {
	godot_audio_input_stop();
	lock();
	input_buffer.clear();
	unlock();
	return OK;
}

int32_t AudioDriverWeb::_new_sample_id() {
	// Skips 0, and any ID still in use should the counter ever wrap.
	do {
		last_sample_id = last_sample_id == INT32_MAX ? 1 : last_sample_id + 1;
	} while (sample_playback_objects.has(last_sample_id));
	return last_sample_id;
}

void AudioDriverWeb::_forget_sample_playback(ObjectID p_playback) {
	const SamplePlayback *playback = sample_playbacks.getptr(p_playback);
	if (playback) {
		sample_playback_objects.erase(playback->id);
		sample_playbacks.erase(p_playback);
	}
}

void AudioDriverWeb::_sweep_sample_streams() {
	// Nothing tells the driver when a registered stream is freed, and its sample can be tens of megabytes of PCM.
	LocalVector<ObjectID> freed;
	for (const KeyValue<ObjectID, SampleStream> &E : sample_streams) {
		if (ObjectDB::get_instance(E.key) == nullptr) {
			freed.push_back(E.key);
		}
	}
	for (const ObjectID &stream : freed) {
		godot_audio_sample_unregister_stream(sample_streams[stream].id);
		sample_streams.erase(stream);
	}
}

void AudioDriverWeb::_sample_stream_changed(ObjectID p_stream) {
	SampleStream *stream = singleton ? singleton->sample_streams.getptr(p_stream) : nullptr;
	if (stream) {
		stream->changed = true;
	}
}

bool AudioDriverWeb::is_stream_registered_as_sample(const Ref<AudioStream> &p_stream) const {
	ERR_FAIL_COND_V_MSG(p_stream.is_null(), false, "Parameter p_stream is null.");
	return sample_streams.has(p_stream->get_instance_id());
}

void AudioDriverWeb::register_sample(const Ref<AudioSample> &p_sample) {
	ERR_FAIL_COND_MSG(p_sample.is_null(), "Parameter p_sample is null.");
	ERR_FAIL_COND_MSG(p_sample->stream.is_null(), "Parameter p_sample->stream is null.");

	_sweep_sample_streams();

	AudioSample::LoopMode loop_mode = p_sample->loop_mode;
	Ref<AudioStream> render_stream = p_sample->stream;
	if (loop_mode == AudioSample::LoopMode::LOOP_PINGPONG || loop_mode == AudioSample::LoopMode::LOOP_BACKWARD) {
		Ref<AudioStreamWAV> wav = render_stream;
		if (wav.is_valid() && wav->get_format() == AudioStreamWAV::FORMAT_IMA_ADPCM) {
			loop_mode = AudioSample::LoopMode::LOOP_FORWARD; // AudioStreamPlaybackWAV loops IMA ADPCM forward only.
		} else if (wav.is_valid()) {
			// JS unrolls these loops from the stream's data as it is, so render it without looping.
			wav = wav->duplicate();
			wav->set_loop_mode(AudioStreamWAV::LOOP_DISABLED);
			render_stream = wav;
		}
	}

	String loop_mode_name;
	switch (loop_mode) {
		case AudioSample::LoopMode::LOOP_DISABLED: {
			loop_mode_name = "disabled";
		} break;

		case AudioSample::LoopMode::LOOP_FORWARD: {
			loop_mode_name = "forward";
		} break;

		case AudioSample::LoopMode::LOOP_PINGPONG: {
			loop_mode_name = "pingpong";
		} break;

		case AudioSample::LoopMode::LOOP_BACKWARD: {
			loop_mode_name = "backward";
		} break;
	}

	// Render the stream at the mix rate, as AudioServer would mix it, into planar left/right channels.
	const double length = render_stream->get_length();
	const int frames_total = mix_rate * length;
	if (length > LONG_SAMPLE_SECONDS) {
		// A sample is decoded whole on its first play, where Stream playback decodes as it plays. Fine for sound
		// effects; for music it is a hitch on the main thread and the whole track in memory as 32-bit floats.
		const String name = p_sample->stream->get_path().is_empty() ? p_sample->stream->get_class() : p_sample->stream->get_path();
		WARN_PRINT(vformat(R"("%s" (%.0f s) plays as a Sample: on the web that decodes the whole stream when it is first played (%.1f MB) instead of streaming it. Long music plays better with "playback_type" set to Stream.)", name, length, frames_total * 2.0 * sizeof(float) / (1024.0 * 1024.0)));
	}
	LocalVector<AudioFrame> frames;
	frames.resize(frames_total);
	int frames_mixed = 0;
	{
		Ref<AudioStreamPlayback> stream_playback = render_stream->instantiate_playback();
		stream_playback->start();
		frames_mixed = CLAMP(stream_playback->mix(frames.ptr(), 1.0f, frames_total), 0, frames_total);
	}
	LocalVector<float> data;
	data.resize(frames_total * 2);
	for (int i = 0; i < frames_total; i++) {
		const AudioFrame frame = i < frames_mixed ? frames[i] : AudioFrame(0, 0);
		data[i] = frame.left;
		data[i + frames_total] = frame.right;
	}

	// Adjust loop start/end to match mix rate.
	const int sample_rate = p_sample->sample_rate;
	int loop_begin = p_sample->loop_begin;
	int loop_end = p_sample->loop_end;
	if (sample_rate != mix_rate) {
		loop_begin = Math::round(loop_begin * (float(mix_rate) / sample_rate));
		loop_end = Math::round(loop_end * (float(mix_rate) / sample_rate));
	}

	const ObjectID stream_object = p_sample->stream->get_instance_id();
	SampleStream *stream = sample_streams.getptr(stream_object);
	if (!stream) {
		stream = &sample_streams.insert(stream_object, SampleStream())->value;
		stream->id = _new_sample_id();
		// Natively a change to the stream applies to its next playback; here it has to be rendered again.
		p_sample->stream->connect(CoreStringName(changed), callable_mp_static(&AudioDriverWeb::_sample_stream_changed).bind(stream_object));
	}
	stream->changed = false;
	godot_audio_sample_register_stream(stream->id, data.ptr(), frames_total, loop_mode_name.utf8().get_data(), loop_begin, loop_end);
}

void AudioDriverWeb::unregister_sample(const Ref<AudioSample> &p_sample) {
	ERR_FAIL_COND_MSG(p_sample.is_null(), "Parameter p_sample is null.");
	ERR_FAIL_COND_MSG(p_sample->stream.is_null(), "Parameter p_sample->stream is null.");

	const ObjectID stream_object = p_sample->stream->get_instance_id();
	const SampleStream *stream = sample_streams.getptr(stream_object);
	if (stream) {
		godot_audio_sample_unregister_stream(stream->id);
		sample_streams.erase(stream_object);
		p_sample->stream->disconnect(CoreStringName(changed), callable_mp_static(&AudioDriverWeb::_sample_stream_changed).bind(stream_object));
	}
}

void AudioDriverWeb::start_sample_playback(const Ref<AudioSamplePlayback> &p_playback) {
	ERR_FAIL_COND_MSG(p_playback.is_null(), "Parameter p_playback is null.");
	ERR_FAIL_COND_MSG(p_playback->stream.is_null(), "Parameter p_playback->stream is null.");
	const SampleStream *stream = sample_streams.getptr(p_playback->stream->get_instance_id());
	ERR_FAIL_NULL_MSG(stream, "The playback's stream is not registered as a sample.");
	if (stream->changed) {
		register_sample(p_playback->stream->generate_sample());
	}
	const int32_t stream_id = stream->id;

	if (++sample_starts_since_sweep >= 64) {
		sample_starts_since_sweep = 0;
		_sweep_sample_streams();
	}

	const ObjectID playback_object = p_playback->get_instance_id();
	SamplePlayback *playback = sample_playbacks.getptr(playback_object);
	if (!playback) {
		playback = &sample_playbacks.insert(playback_object, SamplePlayback())->value;
		playback->id = _new_sample_id();
		sample_playback_objects.insert(playback->id, playback_object);
	}

	constexpr int real_max_channels = AuSC::MAX_CHANNELS_PER_BUS * 2;
	float volume[real_max_channels] = {};
	for (int i = 0; i < MIN(int(p_playback->volume_vector.size()), AuSC::MAX_CHANNELS_PER_BUS); i++) {
		volume[i * 2] = p_playback->volume_vector[i].left;
		volume[i * 2 + 1] = p_playback->volume_vector[i].right;
	}
	// AudioServer mixes to Master when the bus does not exist.
	const int bus = MAX(AudioServer::get_singleton()->get_bus_index(p_playback->bus), 0);

	// A start replaces the JS playback, which then has these settings and no filter.
	playback->pitch_scale = p_playback->pitch_scale;
	playback->highshelf_gain = 0.0;
	playback->highshelf_cutoff_hz = 0.0;
	playback->buses.resize(1);
	playback->buses[0] = bus;
	playback->volumes.resize(real_max_channels);
	memcpy(playback->volumes.ptr(), volume, sizeof(volume));

	godot_audio_sample_start(playback->id, stream_id, bus, p_playback->offset, p_playback->pitch_scale, volume);
}

void AudioDriverWeb::stop_sample_playback(const Ref<AudioSamplePlayback> &p_playback) {
	ERR_FAIL_COND_MSG(p_playback.is_null(), "Parameter p_playback is null.");
	const ObjectID playback_object = p_playback->get_instance_id();
	const SamplePlayback *playback = sample_playbacks.getptr(playback_object);
	if (playback) {
		godot_audio_sample_stop(playback->id);
		_forget_sample_playback(playback_object);
	}
}

void AudioDriverWeb::set_sample_playback_pause(const Ref<AudioSamplePlayback> &p_playback, bool p_paused) {
	ERR_FAIL_COND_MSG(p_playback.is_null(), "Parameter p_playback is null.");
	const SamplePlayback *playback = sample_playbacks.getptr(p_playback->get_instance_id());
	if (playback) {
		godot_audio_sample_set_pause(playback->id, p_paused);
	}
}

bool AudioDriverWeb::is_sample_playback_active(const Ref<AudioSamplePlayback> &p_playback) {
	ERR_FAIL_COND_V_MSG(p_playback.is_null(), false, "Parameter p_playback is null.");
	return sample_playbacks.has(p_playback->get_instance_id());
}

double AudioDriverWeb::get_sample_playback_position(const Ref<AudioSamplePlayback> &p_playback) {
	ERR_FAIL_COND_V_MSG(p_playback.is_null(), 0, "Parameter p_playback is null.");
	const SamplePlayback *playback = sample_playbacks.getptr(p_playback->get_instance_id());
	return playback ? godot_audio_get_sample_playback_position(playback->id) : 0;
}

void AudioDriverWeb::update_sample_playback_pitch_scale(const Ref<AudioSamplePlayback> &p_playback, float p_pitch_scale) {
	ERR_FAIL_COND_MSG(p_playback.is_null(), "Parameter p_playback is null.");
	SamplePlayback *playback = sample_playbacks.getptr(p_playback->get_instance_id());
	if (!playback || playback->pitch_scale == p_pitch_scale) {
		return;
	}
	playback->pitch_scale = p_pitch_scale;
	godot_audio_sample_update_pitch_scale(playback->id, p_pitch_scale);
}

void AudioDriverWeb::set_sample_playback_bus_volumes_linear(const Ref<AudioSamplePlayback> &p_playback, const HashMap<StringName, Vector<AudioFrame>> &p_bus_volumes) {
	ERR_FAIL_COND_MSG(p_playback.is_null(), "Parameter p_playback is null.");
	SamplePlayback *playback = sample_playbacks.getptr(p_playback->get_instance_id());
	if (!playback) {
		return;
	}

	constexpr int real_max_channels = AuSC::MAX_CHANNELS_PER_BUS * 2;
	int32_t buses[AuSC::MAX_BUSES_PER_PLAYBACK];
	float volumes[AuSC::MAX_BUSES_PER_PLAYBACK * real_max_channels];
	int bus_count = 0;
	for (const KeyValue<StringName, Vector<AudioFrame>> &pair : p_bus_volumes) {
		if (bus_count >= AuSC::MAX_BUSES_PER_PLAYBACK) {
			break;
		}
		ERR_FAIL_COND(pair.value.size() != AuSC::MAX_CHANNELS_PER_BUS);
		// AudioServer mixes to Master when the bus does not exist.
		buses[bus_count] = MAX(AudioServer::get_singleton()->get_bus_index(pair.key), 0);
		float *bus_volumes = &volumes[bus_count * real_max_channels];
		for (int i = 0; i < AuSC::MAX_CHANNELS_PER_BUS; i++) {
			bus_volumes[i * 2] = pair.value[i].left;
			bus_volumes[i * 2 + 1] = pair.value[i].right;
		}
		bus_count++;
	}

	// 2D and 3D players send this every frame, nearly always unchanged.
	const int volume_count = bus_count * real_max_channels;
	if (int(playback->buses.size()) == bus_count && memcmp(playback->buses.ptr(), buses, bus_count * sizeof(int32_t)) == 0 && memcmp(playback->volumes.ptr(), volumes, volume_count * sizeof(float)) == 0) {
		return;
	}
	playback->buses.resize(bus_count);
	playback->volumes.resize(volume_count);
	memcpy(playback->buses.ptr(), buses, bus_count * sizeof(int32_t));
	memcpy(playback->volumes.ptr(), volumes, volume_count * sizeof(float));

	godot_audio_sample_set_volumes_linear(playback->id, buses, bus_count, volumes, volume_count);
}

void AudioDriverWeb::set_sample_playback_highshelf_params(const Ref<AudioSamplePlayback> &p_playback, float p_gain, float p_attenuation_cutoff_hz) {
	ERR_FAIL_COND_MSG(p_playback.is_null(), "Parameter p_playback is null.");
	SamplePlayback *playback = sample_playbacks.getptr(p_playback->get_instance_id());
	if (!playback || (playback->highshelf_gain == p_gain && playback->highshelf_cutoff_hz == p_attenuation_cutoff_hz)) {
		return;
	}
	playback->highshelf_gain = p_gain;
	playback->highshelf_cutoff_hz = p_attenuation_cutoff_hz;
	godot_audio_sample_set_highshelf(playback->id, p_gain, p_attenuation_cutoff_hz);
}

void AudioDriverWeb::set_sample_bus_count(int p_count) {
	godot_audio_sample_bus_set_count(p_count);
}

void AudioDriverWeb::remove_sample_bus(int p_index) {
	godot_audio_sample_bus_remove(p_index);
}

void AudioDriverWeb::add_sample_bus(int p_at_pos) {
	godot_audio_sample_bus_add(p_at_pos);
}

void AudioDriverWeb::move_sample_bus(int p_bus, int p_to_pos) {
	godot_audio_sample_bus_move(p_bus, p_to_pos);
}

void AudioDriverWeb::set_sample_bus_send(int p_bus, const StringName &p_send) {
	godot_audio_sample_bus_set_send(p_bus, AudioServer::get_singleton()->get_bus_index(p_send));
}

void AudioDriverWeb::set_sample_bus_volume_db(int p_bus, float p_volume_db) {
	godot_audio_sample_bus_set_volume_db(p_bus, p_volume_db);
}

void AudioDriverWeb::set_sample_bus_solo(int p_bus, bool p_enable) {
	godot_audio_sample_bus_set_solo(p_bus, p_enable);
}

void AudioDriverWeb::set_sample_bus_mute(int p_bus, bool p_enable) {
	godot_audio_sample_bus_set_mute(p_bus, p_enable);
}

#ifdef THREADS_ENABLED

/// AudioWorkletNode implementation (threads)
void AudioDriverWorklet::_audio_thread_func(void *p_data) {
	AudioDriverWorklet *driver = static_cast<AudioDriverWorklet *>(p_data);
	const int out_samples = memarr_len(driver->get_output_rb());
	const int in_samples = memarr_len(driver->get_input_rb());
	int wpos = 0;
	int to_write = out_samples;
	int rpos = 0;
	int to_read = 0;
	int32_t step = 0;
	while (!driver->quit) {
		if (to_read) {
			driver->lock();
			driver->_audio_driver_capture(rpos, to_read);
			godot_audio_worklet_state_add(driver->state, STATE_SAMPLES_IN, -to_read);
			driver->unlock();
			rpos += to_read;
			if (rpos >= in_samples) {
				rpos -= in_samples;
			}
		}
		if (to_write) {
			driver->lock();
			driver->_audio_driver_process(wpos, to_write);
			godot_audio_worklet_state_add(driver->state, STATE_SAMPLES_OUT, to_write);
			driver->unlock();
			wpos += to_write;
			if (wpos >= out_samples) {
				wpos -= out_samples;
			}
		}
		step = godot_audio_worklet_state_wait(driver->state, STATE_PROCESS, step, 1);
		to_write = out_samples - godot_audio_worklet_state_get(driver->state, STATE_SAMPLES_OUT);
		to_read = godot_audio_worklet_state_get(driver->state, STATE_SAMPLES_IN);
	}
}

Error AudioDriverWorklet::create(int &p_buffer_size, int p_channels) {
	if (!godot_audio_has_worklet()) {
		return ERR_UNAVAILABLE;
	}
	return (Error)godot_audio_worklet_create(p_channels);
}

void AudioDriverWorklet::start(float *p_out_buf, int p_out_buf_size, float *p_in_buf, int p_in_buf_size) {
	godot_audio_worklet_start(p_in_buf, p_in_buf_size, p_out_buf, p_out_buf_size, state);
	thread.start(_audio_thread_func, this);
}

void AudioDriverWorklet::lock() {
	mutex.lock();
}

void AudioDriverWorklet::unlock() {
	mutex.unlock();
}

void AudioDriverWorklet::finish_driver() {
	if (thread.is_started()) {
		quit = true; // Ask thread to quit.
		thread.wait_to_finish();
	}
}

#else // No threads.

/// AudioWorkletNode implementation (no threads)
AudioDriverWorklet *AudioDriverWorklet::singleton = nullptr;

Error AudioDriverWorklet::create(int &p_buffer_size, int p_channels) {
	if (!godot_audio_has_worklet()) {
		return ERR_UNAVAILABLE;
	}
	return (Error)godot_audio_worklet_create(p_channels);
}

void AudioDriverWorklet::start(float *p_out_buf, int p_out_buf_size, float *p_in_buf, int p_in_buf_size) {
	_audio_driver_process();
	godot_audio_worklet_start_no_threads(p_out_buf, p_out_buf_size, &_process_callback, p_in_buf, p_in_buf_size, &_capture_callback);
}

void AudioDriverWorklet::_process_callback(int p_pos, int p_samples) {
	AudioDriverWorklet *driver = AudioDriverWorklet::get_singleton();
	driver->_audio_driver_process(p_pos, p_samples);
}

void AudioDriverWorklet::_capture_callback(int p_pos, int p_samples) {
	AudioDriverWorklet *driver = AudioDriverWorklet::get_singleton();
	driver->_audio_driver_capture(p_pos, p_samples);
}

#endif // THREADS_ENABLED

/// ScriptProcessorNode implementation
AudioDriverScriptProcessor *AudioDriverScriptProcessor::singleton = nullptr;

void AudioDriverScriptProcessor::_process_callback() {
	AudioDriverScriptProcessor::get_singleton()->_audio_driver_capture();
	AudioDriverScriptProcessor::get_singleton()->_audio_driver_process();
}

Error AudioDriverScriptProcessor::create(int &p_buffer_samples, int p_channels) {
	if (!godot_audio_has_script_processor()) {
		return ERR_UNAVAILABLE;
	}
	return (Error)godot_audio_script_create(&p_buffer_samples, p_channels);
}

void AudioDriverScriptProcessor::start(float *p_out_buf, int p_out_buf_size, float *p_in_buf, int p_in_buf_size) {
	godot_audio_script_start(p_in_buf, p_in_buf_size, p_out_buf, p_out_buf_size, &_process_callback);
}
