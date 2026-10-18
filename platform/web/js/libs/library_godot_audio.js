/**************************************************************************/
/*  library_godot_audio.js                                                */
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

/**
 * The Web Audio backend of AudioServer's sample playback (AudioStreamPlayer's playback_type = Sample).
 *
 * It reproduces what AudioServer::_mix_step() does with a stream playback, on a Web Audio graph:
 *
 *   AudioBufferSourceNode -> fader -> [high-shelf filter] -> one mixer per bus -> bus volume -> bus mute/solo -> send
 *
 * - Volumes ramp across one mix block when they change, a playback added to a bus starts at full volume, and a
 *   playback removed from a bus, stopped or paused fades out over one block, as AudioServer does.
 * - Buses follow AudioServer's routing rules: a send to a bus that does not exist or is not after the sender goes
 *   to Master, and solo keeps the soloed buses and their send chains audible while ignoring mute.
 * - Loops use the source's native loop points. Ping-pong and backward loops are unrolled into the buffer, so they
 *   play like AudioStreamPlaybackWAV does.
 * - Playback positions are computed from the context clock and the playback rate, not reported by a worklet.
 *
 * @param {!AudioContext} audioContext
 */
function GodotSampleDriver(audioContext) {
	/** @type {?AudioContext} */
	let ctx = audioContext;

	/** @enum {string} */
	const LoopMode = {
		DISABLED: 'disabled',
		FORWARD: 'forward',
		BACKWARD: 'backward',
		PINGPONG: 'pingpong',
	};
	/** @type {number} */
	const sampleRate = ctx.sampleRate;
	// AudioServer mixes in blocks of this many frames: it ramps a playback's volume across one block
	// (AudioServer::_mix_step_for_channel()), and fades a playback out over one block when it stops or pauses.
	const MIX_BLOCK_FRAMES = 512;
	const RAMP_TIME = MIX_BLOCK_FRAMES / sampleRate;
	const RAMP_TIME_CONSTANT = RAMP_TIME / 4; // setTargetAtTime() is within 2% of its target after 4 time constants.
	// A volume vector holds one gain per output channel, in Godot's order (L, R, C, LFE, RL, RR, SL, SR). That is
	// also the Web Audio order of the stereo, 5.1 and 7.1 layouts, the only ones GodotAudio.init() picks.
	const VOLUME_CHANNELS = 8;
	/** @type {number} */
	const outputChannels = Math.min(ctx.destination.channelCount, VOLUME_CHANNELS);

	/**
	 * JS pair of a Godot AudioBus.
	 * @typedef {{
	 *   input: !GainNode,
	 *   output: !GainNode,
	 *   send: ?Object,
	 *   target: (?Object|undefined),
	 *   mute: boolean,
	 *   solo: boolean,
	 *   outputGain: number,
	 * }}
	 */
	var GodotBus; // eslint-disable-line no-var, no-unassigned-vars
	/**
	 * JS pair of a Godot AudioStream/AudioSample.
	 * @typedef {{
	 *   buffer: !AudioBuffer,
	 *   loopMode: LoopMode,
	 *   looping: boolean,
	 *   loopBegin: number,
	 *   loopEnd: number,
	 *   bufferLoopStart: number,
	 *   bufferLoopEnd: number,
	 * }}
	 */
	var GodotStream; // eslint-disable-line no-var, no-unassigned-vars
	/**
	 * JS pair of a Godot AudioSamplePlayback.
	 * @typedef {{
	 *   retire: function(boolean): void,
	 *   pause: function(boolean): void,
	 *   setVolumes: function(!Int32Array, !Float32Array): void,
	 *   setPitchScale: function(number): void,
	 *   setHighshelf: function(number, number): void,
	 *   getPlaybackPosition: function(): number,
	 * }}
	 */
	var GodotPlayback; // eslint-disable-line no-var, no-unassigned-vars

	/** @type {!Array<!GodotBus>} */
	const buses = [];
	/** @type {!Map<number, !GodotStream>} */
	const streams = new Map();
	/** @type {!Map<number, !GodotPlayback>} */
	const playbacks = new Map();
	/** @type {?function(number): void} */
	let finishedCallback = null;

	/** @param {?function(number): void} callback */
	function setFinishedCallback(callback) {
		finishedCallback = callback;
	}

	/**
	 * @param {number} db
	 * @return {number}
	 */
	function dbToLinear(db) {
		return Math.exp(db * 0.11512925464970228420089957273422); // eslint-disable-line no-loss-of-precision
	}

	/** @return {!GodotBus} */
	function createBus() {
		const input = ctx.createGain(); // Bus volume.
		const output = ctx.createGain(); // Mute and solo.
		input.connect(output);
		return {
			input,
			output,
			send: null, // The bus this one sends to, null for Master.
			target: undefined, // The bus the output is connected to, null for the destination, undefined for nothing.
			mute: false,
			solo: false,
			outputGain: 1,
		};
	}
	/** @param {!GodotBus} bus */
	function clearBus(bus) {
		bus.input.disconnect();
		bus.output.disconnect();
	}
	/**
	 * @param {number} index
	 * @return {?GodotBus}
	 */
	function getBus(index) {
		return buses[index] ?? null;
	}

	/**
	 * Applies AudioServer::_mix_step()'s routing rules to the bus graph. A bus sends to Master when its target does
	 * not exist or is not after it. While any bus is soloed, the soloed buses and every bus down their send chains
	 * play and all others are silent, whether muted or not; otherwise the muted buses are silent.
	 */
	function updateRouting() {
		for (let i = 0; i < buses.length; i++) {
			const bus = buses[i];
			let target = null;
			if (i > 0) {
				target = bus.send !== null && buses.indexOf(bus.send) > i ? bus.send : buses[0];
			}
			if (bus.target !== target) {
				if (bus.target !== undefined) {
					bus.output.disconnect();
				}
				bus.output.connect(target === null ? ctx.destination : target.input);
				bus.target = target;
			}
		}
		/** @type {!Set<!GodotBus>} */
		const soloed = new Set();
		for (const bus of buses) {
			if (!bus.solo) {
				continue;
			}
			for (let b = bus; b && !soloed.has(b); b = b.target) {
				soloed.add(b);
			}
		}
		for (const bus of buses) {
			let gain = bus.mute ? 0 : 1;
			if (soloed.size > 0) {
				gain = soloed.has(bus) ? 1 : 0;
			}
			if (bus.outputGain !== gain) {
				bus.outputGain = gain;
				bus.output.gain.value = gain;
			}
		}
	}

	/** @param {number} count */
	function setBusCount(count) {
		buses.splice(count).forEach(clearBus);
		while (buses.length < count) {
			buses.push(createBus()); // New buses send to Master, like in AudioServer::set_bus_count().
		}
		updateRouting();
	}
	/** @param {number} index Already resolved by AudioServer::add_bus(): -1 appends. */
	function addBusAt(index) {
		const bus = createBus();
		if (index < 0 || index >= buses.length) {
			buses.push(bus);
		} else {
			buses.splice(index, 0, bus);
		}
		updateRouting();
	}
	/**
	 * Same as AudioServer::move_bus().
	 * @param {number} fromIndex
	 * @param {number} toIndex
	 */
	function moveBus(fromIndex, toIndex) {
		const bus = getBus(fromIndex);
		if (!bus || fromIndex === toIndex) {
			return;
		}
		buses.splice(fromIndex, 1);
		if (toIndex < 0) {
			buses.push(bus);
		} else if (toIndex < fromIndex) {
			buses.splice(toIndex, 0, bus);
		} else {
			buses.splice(toIndex - 1, 0, bus);
		}
		updateRouting();
	}
	/** @param {number} index */
	function removeBus(index) {
		const bus = getBus(index);
		if (!bus) {
			return;
		}
		buses.splice(index, 1);
		clearBus(bus);
		updateRouting(); // Buses that sent to it now send to Master.
	}
	/**
	 * @param {number} index
	 * @param {number} sendIndex -1 for Master.
	 */
	function setBusSend(index, sendIndex) {
		const bus = getBus(index);
		if (!bus) {
			return;
		}
		bus.send = getBus(sendIndex);
		updateRouting();
	}
	/**
	 * @param {number} index
	 * @param {number} db
	 */
	function setBusVolumeDb(index, db) {
		const bus = getBus(index);
		const linear = dbToLinear(db);
		if (bus && !Number.isNaN(linear)) {
			bus.input.gain.value = linear; // AudioServer applies bus volumes per block, without a ramp.
		}
	}
	/**
	 * @param {number} index
	 * @param {boolean} enable
	 */
	function setBusMute(index, enable) {
		const bus = getBus(index);
		if (bus) {
			bus.mute = enable;
			updateRouting();
		}
	}
	/**
	 * @param {number} index
	 * @param {boolean} enable
	 */
	function setBusSolo(index, enable) {
		const bus = getBus(index);
		if (bus) {
			bus.solo = enable;
			updateRouting();
		}
	}

	/**
	 * Creates the AudioBuffer of a sample. Every playback of the stream shares it.
	 * @param {number} streamId
	 * @param {!Float32Array} left
	 * @param {!Float32Array} right
	 * @param {LoopMode} loopMode
	 * @param {number} loopBegin In frames at the context's sample rate.
	 * @param {number} loopEnd In frames at the context's sample rate, 0 for the end of the stream.
	 */
	function createStream(streamId, left, right, loopMode, loopBegin, loopEnd) {
		const frames = left.length;
		const end = loopEnd > 0 ? Math.min(loopEnd, frames) : frames;
		const begin = Math.min(Math.max(loopBegin, 0), end);
		const looping = loopMode !== LoopMode.DISABLED && end > begin;
		// AudioStreamPlaybackWAV plays a ping-pong loop forward to loop_end, mirrors back down to loop_begin and so on
		// (..., end - 1, end, end - 1, ..., begin + 1, begin, begin + 1, ...), and a backward loop from its start
		// position down to loop_begin, then from loop_end - 1 down to loop_begin over and over. Lay both out in a
		// buffer that loops forward: ping-pong unrolled, backward reversed whole.
		let length = frames;
		let bufferLoopStart = begin;
		let bufferLoopEnd = end;
		if (looping && loopMode === LoopMode.PINGPONG) {
			length = end + (end - begin);
			bufferLoopEnd = length;
		} else if (looping && loopMode === LoopMode.BACKWARD) {
			bufferLoopStart = frames - end;
			bufferLoopEnd = frames - begin;
		}
		const buffer = ctx.createBuffer(2, Math.max(length, 1), sampleRate);
		// Writing through getChannelData() also accepts views of a SharedArrayBuffer heap, which copyToChannel() does not.
		[left, right].forEach((src, ch) => {
			const data = buffer.getChannelData(ch);
			if (looping && loopMode === LoopMode.PINGPONG) {
				data.set(src.subarray(0, end));
				for (let i = 0; i < end - begin; i++) {
					data[end + i] = src[Math.min(end - i, frames - 1)];
				}
			} else if (looping && loopMode === LoopMode.BACKWARD) {
				for (let i = 0; i < frames; i++) {
					data[i] = src[frames - 1 - i];
				}
			} else {
				data.set(src);
			}
		});
		streams.set(streamId, {
			buffer,
			loopMode: looping ? loopMode : LoopMode.DISABLED,
			looping,
			loopBegin: begin,
			loopEnd: end,
			bufferLoopStart,
			bufferLoopEnd,
		});
	}
	/**
	 * @param {number} id
	 * @return {?GodotStream}
	 */
	function getStream(id) {
		return streams.get(id) ?? null;
	}
	/** @param {number} id */
	function removeStream(id) {
		streams.delete(id); // Playbacks still using the buffer keep it alive.
	}

	/**
	 * @param {!GodotStream} stream
	 * @param {number} position In frames, may be past the buffer's loop end.
	 * @return {number}
	 */
	function wrapBufferPosition(stream, position) {
		if (!stream.looping || position < stream.bufferLoopEnd) {
			return position;
		}
		const start = stream.bufferLoopStart;
		return start + ((position - start) % (stream.bufferLoopEnd - start));
	}
	/**
	 * Maps a stream position (what AudioStreamPlaybackWAV seeks to) to the position in the stream's buffer.
	 * @param {!GodotStream} stream
	 * @param {number} position In frames.
	 * @return {number}
	 */
	function streamToBufferPosition(stream, position) {
		const begin = stream.loopBegin;
		const end = stream.loopEnd;
		if (stream.loopMode === LoopMode.BACKWARD) {
			// Played backward from the start position; below loop_begin it wraps to loop_end.
			const last = stream.buffer.length - 1;
			let pos = Math.min(position, last);
			while (pos < begin) {
				pos = end - (begin - pos);
			}
			return last - pos;
		}
		return wrapBufferPosition(stream, position);
	}
	/**
	 * Maps a position in the stream's buffer back to the stream position AudioStreamPlaybackWAV would report.
	 * @param {!GodotStream} stream
	 * @param {number} position In frames, inside the buffer.
	 * @return {number}
	 */
	function bufferToStreamPosition(stream, position) {
		const end = stream.loopEnd;
		if (stream.loopMode === LoopMode.PINGPONG && position >= end) {
			return Math.max(2 * end - position, 0);
		} else if (stream.loopMode === LoopMode.BACKWARD) {
			return Math.max(stream.buffer.length - 1 - position, 0);
		}
		return position;
	}

	/**
	 * Routes a playback to one bus, with one gain per output channel.
	 * @param {!GodotBus} bus
	 * @param {!AudioNode} from
	 */
	function createMixer(bus, from) {
		const splitter = ctx.createChannelSplitter(2);
		const merger = ctx.createChannelMerger(outputChannels);
		/** @type {!Array<!GainNode>} */
		const gains = [];
		// The last gain written per channel. Volumes only reach JS when they change, but a mixer only ramps the
		// channels that did.
		const written = new Float32Array(outputChannels);
		for (let ch = 0; ch < outputChannels; ch++) {
			const gain = ctx.createGain();
			gain.gain.value = 0;
			splitter.connect(gain, ch % 2);
			gain.connect(merger, 0, ch);
			gains.push(gain);
		}
		merger.connect(bus.input);
		from.connect(splitter);
		let active = false;
		return {
			/**
			 * @param {!Float32Array} volumes
			 * @param {number} offset
			 */
			setVolume(volumes, offset) {
				const now = ctx.currentTime;
				let audible = false;
				for (let ch = 0; ch < outputChannels; ch++) {
					const value = volumes[offset + ch];
					audible ||= value !== 0;
					if (written[ch] === value) {
						continue;
					}
					written[ch] = value;
					if (active) {
						gains[ch].gain.setTargetAtTime(value, now, RAMP_TIME_CONSTANT);
					} else {
						// A playback starts on a bus at full volume. 2D and 3D players start silent and get their
						// volumes on the next frame, which counts as the start.
						gains[ch].gain.value = value;
					}
				}
				active ||= audible;
			},
			/** Fades out of the bus, as when AudioServer stops mixing a playback to a bus. */
			fadeOut() {
				if (!active) {
					return;
				}
				const now = ctx.currentTime;
				for (let ch = 0; ch < outputChannels; ch++) {
					written[ch] = 0;
					gains[ch].gain.setTargetAtTime(0, now, RAMP_TIME_CONSTANT);
				}
				active = false;
			},
			/** @param {!AudioNode} newFrom */
			reconnect(newFrom) {
				newFrom.connect(splitter);
			},
			clear() {
				splitter.disconnect();
				gains.forEach((gain) => gain.disconnect());
				merger.disconnect();
			},
		};
	}

	/**
	 * @param {number} playbackId
	 * @param {number} streamId
	 * @param {number} pitchScale
	 * @return {?GodotPlayback}
	 */
	function createPlayback(playbackId, streamId, pitchScale) {
		const stream = getStream(streamId);
		if (!stream) {
			return null;
		}
		const source = ctx.createBufferSource();
		source.buffer = stream.buffer;
		if (stream.looping) {
			source.loop = true;
			source.loopStart = stream.bufferLoopStart / sampleRate;
			source.loopEnd = stream.bufferLoopEnd / sampleRate;
		}
		const fader = ctx.createGain();
		source.connect(fader);
		/** @type {?BiquadFilterNode} */
		let filter = null;
		/** @type {!AudioNode} */
		let tail = fader; // What the mixers listen to.
		/** @type {!Map<!GodotBus, !Object>} */
		const mixers = new Map();

		let paused = false;
		let retired = false;
		let scale = pitchScale;
		// The position is (time - anchorTime) * rate frames after anchorFrames, in the buffer.
		let anchorTime = 0;
		let anchorFrames = 0;
		let rate = 0;
		/**
		 * @param {number} time
		 * @return {number}
		 */
		function framesAt(time) {
			return anchorFrames + (Math.max(time - anchorTime, 0) * sampleRate * rate);
		}
		/**
		 * @param {number} newRate
		 * @param {number} time
		 */
		function setRate(newRate, time) {
			anchorFrames = framesAt(time);
			anchorTime = time;
			rate = newRate;
		}

		function teardown() {
			source.disconnect();
			fader.disconnect();
			if (filter) {
				filter.disconnect();
			}
			mixers.forEach((mixer) => mixer.clear());
			mixers.clear();
		}
		/**
		 * Stops the playback for good.
		 * @param {boolean} fade Fade out over one mix block, like AudioServer::stop_playback_stream().
		 */
		function retire(fade) {
			if (retired) {
				return;
			}
			retired = true;
			if (fade && !paused) {
				const now = ctx.currentTime;
				fader.gain.setTargetAtTime(0, now, RAMP_TIME_CONSTANT);
				source.onended = teardown;
				source.stop(now + RAMP_TIME);
			} else {
				source.onended = null;
				source.stop();
				teardown();
			}
		}
		/** @param {number} position In seconds, in the stream. */
		function start(position) {
			const frames = streamToBufferPosition(stream, position * sampleRate);
			source.playbackRate.value = scale;
			source.start(0, frames / sampleRate);
			anchorTime = ctx.currentTime;
			anchorFrames = frames;
			rate = scale;
		}
		/** @param {number} newScale */
		function setPitchScale(newScale) {
			if (scale === newScale) {
				return;
			}
			scale = newScale;
			if (paused || retired) {
				return;
			}
			setRate(scale, ctx.currentTime);
			source.playbackRate.value = scale;
		}
		/** @param {boolean} enable */
		function pause(enable) {
			if (paused === enable || retired) {
				return;
			}
			paused = enable;
			const now = ctx.currentTime;
			if (enable) {
				// Fade out over one block, then hold the source in place (AudioServer's FADE_OUT_TO_PAUSE).
				fader.gain.setTargetAtTime(0, now, RAMP_TIME_CONSTANT);
				source.playbackRate.setValueAtTime(0, now + RAMP_TIME);
				setRate(rate, now);
				setRate(0, now + RAMP_TIME);
			} else {
				source.playbackRate.cancelScheduledValues(now);
				source.playbackRate.value = scale;
				fader.gain.setTargetAtTime(1, now, RAMP_TIME_CONSTANT);
				anchorFrames = framesAt(now);
				anchorTime = now;
				rate = scale;
			}
		}
		/**
		 * @param {!Int32Array} busIndexes
		 * @param {!Float32Array} volumes VOLUME_CHANNELS per bus. Not kept.
		 */
		function setVolumes(busIndexes, volumes) {
			if (retired) {
				return;
			}
			/** @type {!Set<!GodotBus>} */
			const wanted = new Set();
			for (let i = 0; i < busIndexes.length; i++) {
				const bus = getBus(busIndexes[i]);
				if (!bus || wanted.has(bus)) {
					continue;
				}
				wanted.add(bus);
				let mixer = mixers.get(bus);
				if (!mixer) {
					mixer = createMixer(bus, tail);
					mixers.set(bus, mixer);
				}
				mixer.setVolume(volumes, i * VOLUME_CHANNELS);
			}
			// A bus the playback no longer mixes to fades out; its mixer stays, silent, should it come back.
			mixers.forEach((mixer, bus) => {
				if (!wanted.has(bus)) {
					mixer.fadeOut();
				}
			});
		}
		/**
		 * The 3D attenuation filter of AudioServer::_mix_step_for_channel().
		 * @param {number} gain Linear; 0 for no filter.
		 * @param {number} cutoffHz
		 */
		function setHighshelf(gain, cutoffHz) {
			if (retired) {
				return;
			}
			// AudioFilterSW's high shelf uses the gain where the RBJ cookbook, and so BiquadFilterNode, use
			// A = 10^(dB / 40). Its transition is a little steeper (Q = 1 where BiquadFilterNode uses S = 1).
			const db = gain > 0 ? 40 * Math.log10(Math.max(gain, 0.001)) : 0;
			if (!filter) {
				if (gain === 0) {
					return;
				}
				filter = ctx.createBiquadFilter();
				filter.type = 'highshelf';
				filter.frequency.value = cutoffHz;
				filter.gain.value = db;
				fader.disconnect();
				fader.connect(filter);
				mixers.forEach((mixer) => mixer.reconnect(filter));
				tail = filter;
				return;
			}
			const now = ctx.currentTime;
			filter.frequency.setTargetAtTime(cutoffHz, now, RAMP_TIME_CONSTANT);
			filter.gain.setTargetAtTime(db, now, RAMP_TIME_CONSTANT);
		}
		/** @return {number} In seconds. */
		function getPlaybackPosition() {
			let frames = framesAt(ctx.currentTime);
			if (stream.looping) {
				frames = wrapBufferPosition(stream, frames);
			} else {
				frames = Math.min(frames, stream.buffer.length);
			}
			return bufferToStreamPosition(stream, frames) / sampleRate;
		}

		const node = {
			start,
			retire,
			pause,
			setVolumes,
			setPitchScale,
			setHighshelf,
			getPlaybackPosition,
		};
		source.onended = () => {
			// Reached the end of a non-looping stream.
			if (retired) {
				return;
			}
			retire(false);
			if (playbacks.get(playbackId) === node) {
				playbacks.delete(playbackId);
			}
			if (finishedCallback) {
				finishedCallback(playbackId);
			}
		};
		return node;
	}
	/**
	 * @param {number} id
	 * @return {?GodotPlayback}
	 */
	function getPlayback(id) {
		return playbacks.get(id) ?? null;
	}
	/** @param {number} id */
	function removePlayback(id) {
		const playback = playbacks.get(id);
		if (playback) {
			playbacks.delete(id);
			playback.retire(true);
		}
	}

	/**
	 * Starts a new playback, replacing (and fading out) the one with that ID, if any.
	 * @param {number} playbackId
	 * @param {number} streamId
	 * @param {number} position In seconds.
	 * @param {number} pitchScale
	 * @param {!Int32Array} busIndexes
	 * @param {!Float32Array} volumes
	 */
	function startPlayback(playbackId, streamId, position, pitchScale, busIndexes, volumes) {
		removePlayback(playbackId);
		const playback = createPlayback(playbackId, streamId, pitchScale);
		if (!playback) {
			return;
		}
		playback.setVolumes(busIndexes, volumes);
		playback.start(position);
		playbacks.set(playbackId, playback);
	}
	function clear() {
		playbacks.forEach((p) => p.retire(false));
		playbacks.clear();
		streams.clear();
		buses.splice(0).forEach(clearBus);
		ctx = null;
	}

	const driver = /** @struct */ {
		setFinishedCallback,
		clear,
		// Streams
		createStream,
		getStream,
		removeStream,
		// Playbacks
		startPlayback,
		getPlayback,
		removePlayback,
		// Buses
		setBusCount,
		addBusAt,
		moveBus,
		removeBus,
		setBusSend,
		setBusVolumeDb,
		setBusMute,
		setBusSolo,
		// So they survive pre-stripping.
		_: { GodotBus, GodotStream, GodotPlayback },
	};
	return driver;
}

const _GodotAudio = {
	$GodotAudio__deps: ['$GodotRuntime', '$GodotOS'],
	$GodotAudio: {
		/** @type {AudioContext} */
		ctx: null,
		input: null,
		driver: null,
		sample_driver: null,
		interval: 0,

		GodotSampleDriver,
		init: function (mix_rate, latency, onstatechange, onlatencyupdate) {
			const opts = {};
			// If mix_rate is 0, let the browser choose.
			if (mix_rate) {
				GodotAudio.sampleRate = mix_rate;
				opts['sampleRate'] = mix_rate;
			}
			// Do not specify, leave 'interactive' for good performance.
			// opts['latencyHint'] = latency / 1000;
			const ctx = new (window.AudioContext || window.webkitAudioContext)(opts);
			GodotAudio.ctx = ctx;
			// Use the device's channels, like the native drivers do, but only in the layouts where Godot's channel
			// order (L, R, C, LFE, RL, RR, SL, SR) is also Web Audio's: stereo, 5.1 and 7.1.
			const maxChannels = ctx.destination.maxChannelCount;
			let channels = 2;
			if (maxChannels >= 8) {
				channels = 8;
			} else if (maxChannels >= 6) {
				channels = 6;
			}
			if (ctx.destination.channelCount !== channels) {
				try {
					ctx.destination.channelCount = channels;
				} catch (e) {
					GodotRuntime.print(`Could not output ${channels} audio channels, using ${ctx.destination.channelCount}.`);
				}
			}
			ctx.onstatechange = function () {
				let state = 0;
				switch (ctx.state) {
				case 'suspended':
					state = 0;
					break;
				case 'running':
					state = 1;
					break;
				case 'closed':
					state = 2;
					break;
				default:
					// Do nothing.
				}
				onstatechange(state);
			};
			ctx.onstatechange(); // Immediately notify state.
			// Update computed latency
			GodotAudio.interval = setInterval(function () {
				let computed_latency = 0;
				if (ctx.baseLatency) {
					computed_latency += GodotAudio.ctx.baseLatency;
				}
				if (ctx.outputLatency) {
					computed_latency += GodotAudio.ctx.outputLatency;
				}
				onlatencyupdate(computed_latency);
			}, 1000);
			GodotOS.atexit(GodotAudio.close_async);

			// Initialize sample driver.
			GodotAudio.sample_driver = GodotAudio.GodotSampleDriver(ctx);

			return ctx.destination.channelCount;
		},

		create_input: function (callback) {
			if (GodotAudio.input) {
				return 0; // Already started.
			}
			function gotMediaInput(stream) {
				try {
					GodotAudio.input = GodotAudio.ctx.createMediaStreamSource(stream);
					callback(GodotAudio.input);
				} catch (e) {
					GodotRuntime.error('Failed creating input.', e);
				}
			}
			if (navigator.mediaDevices && navigator.mediaDevices.getUserMedia) {
				navigator.mediaDevices.getUserMedia({
					'audio': true,
				}).then(gotMediaInput, function (e) {
					GodotRuntime.error('Error getting user media.', e);
				});
			} else {
				if (!navigator.getUserMedia) {
					navigator.getUserMedia = navigator.webkitGetUserMedia || navigator.mozGetUserMedia;
				}
				if (!navigator.getUserMedia) {
					GodotRuntime.error('getUserMedia not available.');
					return 1;
				}
				navigator.getUserMedia({
					'audio': true,
				}, gotMediaInput, function (e) {
					GodotRuntime.print(e);
				});
			}
			return 0;
		},

		close_async: function (resolve, reject) {
			const ctx = GodotAudio.ctx;
			GodotAudio.ctx = null;
			// Audio was not initialized.
			if (!ctx) {
				resolve();
				return;
			}
			// Clear audio driver
			if (GodotAudio.sample_driver) {
				GodotAudio.sample_driver.clear();
				GodotAudio.sample_driver = null;
			}
			// Remove latency callback
			if (GodotAudio.interval) {
				clearInterval(GodotAudio.interval);
				GodotAudio.interval = 0;
			}
			// Disconnect input, if it was started.
			if (GodotAudio.input) {
				GodotAudio.input.disconnect();
				GodotAudio.input = null;
			}
			// Disconnect output
			let closed = Promise.resolve();
			if (GodotAudio.driver) {
				closed = GodotAudio.driver.close();
			}
			closed.then(function () {
				return ctx.close();
			}).then(function () {
				ctx.onstatechange = null;
				resolve();
			}).catch(function (e) {
				ctx.onstatechange = null;
				GodotRuntime.error('Error closing AudioContext', e);
				resolve();
			});
		},
	},

	godot_audio_is_available__sig: 'i',
	godot_audio_is_available__proxy: 'sync',
	godot_audio_is_available: function () {
		if (!(window.AudioContext || window.webkitAudioContext)) {
			return 0;
		}
		return 1;
	},

	godot_audio_has_worklet__proxy: 'sync',
	godot_audio_has_worklet__sig: 'i',
	godot_audio_has_worklet: function () {
		return GodotAudio.ctx && GodotAudio.ctx.audioWorklet ? 1 : 0;
	},

	godot_audio_has_script_processor__proxy: 'sync',
	godot_audio_has_script_processor__sig: 'i',
	godot_audio_has_script_processor: function () {
		return GodotAudio.ctx && GodotAudio.ctx.createScriptProcessor ? 1 : 0;
	},

	godot_audio_init__proxy: 'sync',
	godot_audio_init__sig: 'iiiii',
	godot_audio_init: function (
		p_mix_rate,
		p_latency,
		p_state_change,
		p_latency_update
	) {
		const statechange = GodotRuntime.get_func(p_state_change);
		const latencyupdate = GodotRuntime.get_func(p_latency_update);
		const mix_rate = GodotRuntime.getHeapValue(p_mix_rate, 'i32');
		const channels = GodotAudio.init(
			mix_rate,
			p_latency,
			statechange,
			latencyupdate
		);
		GodotRuntime.setHeapValue(p_mix_rate, GodotAudio.ctx.sampleRate, 'i32');
		return channels;
	},

	godot_audio_resume__proxy: 'sync',
	godot_audio_resume__sig: 'v',
	godot_audio_resume: function () {
		if (GodotAudio.ctx && GodotAudio.ctx.state !== 'running') {
			GodotAudio.ctx.resume();
		}
	},

	godot_audio_input_start__proxy: 'sync',
	godot_audio_input_start__sig: 'i',
	godot_audio_input_start: function () {
		return GodotAudio.create_input(function (input) {
			input.connect(GodotAudio.driver.get_node());
		});
	},

	godot_audio_input_stop__proxy: 'sync',
	godot_audio_input_stop__sig: 'v',
	godot_audio_input_stop: function () {
		if (GodotAudio.input) {
			const tracks = GodotAudio.input['mediaStream']['getTracks']();
			for (let i = 0; i < tracks.length; i++) {
				tracks[i]['stop']();
			}
			GodotAudio.input.disconnect();
			GodotAudio.input = null;
		}
	},
};

autoAddDeps(_GodotAudio, '$GodotAudio');
mergeInto(LibraryManager.library, _GodotAudio);

/**
 * The audio sample driver used by AudioStreamPlayer with playback_type = Sample.
 * Streams and playbacks are identified by small integer handles that AudioDriverWeb assigns.
 */
const _GodotAudioSampleDriver = {
	$GodotAudioSampleDriver__deps: ['$GodotAudio', '$GodotRuntime'],
	$GodotAudioSampleDriver: {},

	godot_audio_sample_register_stream__proxy: 'sync',
	godot_audio_sample_register_stream__sig: 'viiiiii',
	godot_audio_sample_register_stream: function (p_id, p_frames, p_frames_count, p_loop_mode, p_loop_begin, p_loop_end) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		const BYTES_PER_FLOAT32 = 4;
		const left = GodotRuntime.heapSub(HEAPF32, p_frames, p_frames_count);
		const right = GodotRuntime.heapSub(HEAPF32, p_frames + p_frames_count * BYTES_PER_FLOAT32, p_frames_count);
		const loopMode = GodotRuntime.parseString(p_loop_mode);
		GodotAudio.sample_driver.createStream(p_id, left, right, loopMode, p_loop_begin, p_loop_end);
	},

	godot_audio_sample_unregister_stream__proxy: 'sync',
	godot_audio_sample_unregister_stream__sig: 'vi',
	godot_audio_sample_unregister_stream: function (p_id) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.removeStream(p_id);
	},

	godot_audio_sample_start__proxy: 'sync',
	godot_audio_sample_start__sig: 'viiiffi',
	godot_audio_sample_start: function (p_playback, p_stream, p_bus, p_offset, p_pitch_scale, p_volume) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		const VOLUME_CHANNELS = 8;
		const volume = GodotRuntime.heapSub(HEAPF32, p_volume, VOLUME_CHANNELS); // Not kept.
		GodotAudio.sample_driver.startPlayback(p_playback, p_stream, p_offset, p_pitch_scale, Int32Array.of(p_bus), volume);
	},

	godot_audio_sample_stop__proxy: 'sync',
	godot_audio_sample_stop__sig: 'vi',
	godot_audio_sample_stop: function (p_id) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.removePlayback(p_id);
	},

	godot_audio_sample_set_pause__proxy: 'sync',
	godot_audio_sample_set_pause__sig: 'vii',
	godot_audio_sample_set_pause: function (p_id, p_pause) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		const playback = GodotAudio.sample_driver.getPlayback(p_id);
		if (playback) {
			playback.pause(Boolean(p_pause));
		}
	},

	godot_audio_get_sample_playback_position__proxy: 'sync',
	godot_audio_get_sample_playback_position__sig: 'di',
	godot_audio_get_sample_playback_position: function (p_id) {
		if (!GodotAudio.sample_driver) {
			return 0;
		}
		const playback = GodotAudio.sample_driver.getPlayback(p_id);
		return playback ? playback.getPlaybackPosition() : 0;
	},

	godot_audio_sample_update_pitch_scale__proxy: 'sync',
	godot_audio_sample_update_pitch_scale__sig: 'vif',
	godot_audio_sample_update_pitch_scale: function (p_id, p_pitch_scale) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		const playback = GodotAudio.sample_driver.getPlayback(p_id);
		if (playback) {
			playback.setPitchScale(p_pitch_scale);
		}
	},

	godot_audio_sample_set_volumes_linear__proxy: 'sync',
	godot_audio_sample_set_volumes_linear__sig: 'viiiii',
	godot_audio_sample_set_volumes_linear: function (p_id, p_buses, p_buses_size, p_volume, p_volume_size) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		const playback = GodotAudio.sample_driver.getPlayback(p_id);
		if (!playback) {
			return;
		}
		// Views, not copies: the playback does not keep them.
		const buses = GodotRuntime.heapSub(HEAP32, p_buses, p_buses_size);
		const volumes = GodotRuntime.heapSub(HEAPF32, p_volume, p_volume_size);
		playback.setVolumes(buses, volumes);
	},

	godot_audio_sample_set_highshelf__proxy: 'sync',
	godot_audio_sample_set_highshelf__sig: 'viff',
	godot_audio_sample_set_highshelf: function (p_id, p_gain, p_cutoff_hz) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		const playback = GodotAudio.sample_driver.getPlayback(p_id);
		if (playback) {
			playback.setHighshelf(p_gain, p_cutoff_hz);
		}
	},

	godot_audio_sample_bus_set_count__proxy: 'sync',
	godot_audio_sample_bus_set_count__sig: 'vi',
	godot_audio_sample_bus_set_count: function (p_count) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.setBusCount(p_count);
	},

	godot_audio_sample_bus_remove__proxy: 'sync',
	godot_audio_sample_bus_remove__sig: 'vi',
	godot_audio_sample_bus_remove: function (p_index) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.removeBus(p_index);
	},

	godot_audio_sample_bus_add__proxy: 'sync',
	godot_audio_sample_bus_add__sig: 'vi',
	godot_audio_sample_bus_add: function (p_index) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.addBusAt(p_index);
	},

	godot_audio_sample_bus_move__proxy: 'sync',
	godot_audio_sample_bus_move__sig: 'vii',
	godot_audio_sample_bus_move: function (p_from, p_to) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.moveBus(p_from, p_to);
	},

	godot_audio_sample_bus_set_send__proxy: 'sync',
	godot_audio_sample_bus_set_send__sig: 'vii',
	godot_audio_sample_bus_set_send: function (p_index, p_send_index) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.setBusSend(p_index, p_send_index);
	},

	godot_audio_sample_bus_set_volume_db__proxy: 'sync',
	godot_audio_sample_bus_set_volume_db__sig: 'vif',
	godot_audio_sample_bus_set_volume_db: function (p_bus, p_volume_db) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.setBusVolumeDb(p_bus, p_volume_db);
	},

	godot_audio_sample_bus_set_solo__proxy: 'sync',
	godot_audio_sample_bus_set_solo__sig: 'vii',
	godot_audio_sample_bus_set_solo: function (p_bus, p_enable) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.setBusSolo(p_bus, Boolean(p_enable));
	},

	godot_audio_sample_bus_set_mute__proxy: 'sync',
	godot_audio_sample_bus_set_mute__sig: 'vii',
	godot_audio_sample_bus_set_mute: function (p_bus, p_enable) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.setBusMute(p_bus, Boolean(p_enable));
	},

	godot_audio_sample_set_finished_callback__proxy: 'sync',
	godot_audio_sample_set_finished_callback__sig: 'vi',
	godot_audio_sample_set_finished_callback: function (p_callback) {
		if (!GodotAudio.sample_driver) {
			return;
		}
		GodotAudio.sample_driver.setFinishedCallback(GodotRuntime.get_func(p_callback));
	},
};

autoAddDeps(_GodotAudioSampleDriver, '$GodotAudioSampleDriver');
mergeInto(LibraryManager.library, _GodotAudioSampleDriver);

/**
 * The AudioWorklet API driver, used when threads are available.
 */
const GodotAudioWorklet = {
	$GodotAudioWorklet__deps: ['$GodotAudio', '$GodotConfig'],
	$GodotAudioWorklet: {
		promise: null,
		worklet: null,
		ring_buffer: null,

		create: function (channels) {
			const path = GodotConfig.locate_file('godot.audio.worklet.js');
			GodotAudioWorklet.promise = GodotAudio.ctx.audioWorklet
				.addModule(path)
				.then(function () {
					GodotAudioWorklet.worklet = new AudioWorkletNode(
						GodotAudio.ctx,
						'godot-processor',
						{
							outputChannelCount: [channels],
							// Godot's input buffer is interleaved stereo: up-mix a mono microphone instead of
							// handing over its samples as if they were stereo frames.
							channelCount: 2,
							channelCountMode: 'explicit',
							channelInterpretation: 'speakers',
						}
					);
					return Promise.resolve();
				});
			GodotAudio.driver = GodotAudioWorklet;
		},

		start: function (in_buf, out_buf, state) {
			GodotAudioWorklet.promise.then(function () {
				const node = GodotAudioWorklet.worklet;
				node.connect(GodotAudio.ctx.destination);
				node.port.postMessage({
					'cmd': 'start',
					'data': [state, in_buf, out_buf],
				});
				node.port.onmessage = function (event) {
					GodotRuntime.error(event.data);
				};
			});
		},

		start_no_threads: function (
			p_out_buf,
			p_out_size,
			out_callback,
			p_in_buf,
			p_in_size,
			in_callback
		) {
			/** @constructor */
			function RingBuffer() {
				let wpos = 0;
				let rpos = 0;
				let pending_samples = 0;
				const wbuf = new Float32Array(p_out_size);

				function send(port) {
					if (pending_samples === 0) {
						return;
					}
					const buffer = GodotRuntime.heapSub(HEAPF32, p_out_buf, p_out_size);
					const size = buffer.length;
					const tot_sent = pending_samples;
					out_callback(wpos, pending_samples);
					if (wpos + pending_samples >= size) {
						const high = size - wpos;
						wbuf.set(buffer.subarray(wpos, size));
						pending_samples -= high;
						wpos = 0;
					}
					if (pending_samples > 0) {
						wbuf.set(
							buffer.subarray(wpos, wpos + pending_samples),
							tot_sent - pending_samples
						);
					}
					port.postMessage({ 'cmd': 'chunk', 'data': wbuf.subarray(0, tot_sent) });
					wpos += pending_samples;
					pending_samples = 0;
				}
				this.receive = function (recv_buf) {
					const buffer = GodotRuntime.heapSub(HEAPF32, p_in_buf, p_in_size);
					const from = rpos;
					let to_write = recv_buf.length;
					let high = 0;
					if (rpos + to_write >= p_in_size) {
						high = p_in_size - rpos;
						buffer.set(recv_buf.subarray(0, high), rpos);
						to_write -= high;
						rpos = 0;
					}
					if (to_write) {
						buffer.set(recv_buf.subarray(high, high + to_write), rpos);
					}
					in_callback(from, recv_buf.length);
					rpos += to_write;
				};
				this.consumed = function (size, port) {
					pending_samples += size;
					send(port);
				};
			}
			GodotAudioWorklet.ring_buffer = new RingBuffer();
			GodotAudioWorklet.promise.then(function () {
				const node = GodotAudioWorklet.worklet;
				const buffer = GodotRuntime.heapSlice(HEAPF32, p_out_buf, p_out_size);
				node.connect(GodotAudio.ctx.destination);
				node.port.postMessage({
					'cmd': 'start_nothreads',
					'data': [buffer, p_in_size],
				});
				node.port.onmessage = function (event) {
					if (!GodotAudioWorklet.worklet) {
						return;
					}
					if (event.data['cmd'] === 'read') {
						const read = event.data['data'];
						GodotAudioWorklet.ring_buffer.consumed(
							read,
							GodotAudioWorklet.worklet.port
						);
					} else if (event.data['cmd'] === 'input') {
						const buf = event.data['data'];
						if (buf.length > p_in_size) {
							GodotRuntime.error('Input chunk is too big');
							return;
						}
						GodotAudioWorklet.ring_buffer.receive(buf);
					} else {
						GodotRuntime.error(event.data);
					}
				};
			});
		},

		get_node: function () {
			return GodotAudioWorklet.worklet;
		},

		close: function () {
			return new Promise(function (resolve, reject) {
				if (GodotAudioWorklet.promise === null) {
					return;
				}
				const p = GodotAudioWorklet.promise;
				p.then(function () {
					GodotAudioWorklet.worklet.port.postMessage({
						'cmd': 'stop',
						'data': null,
					});
					GodotAudioWorklet.worklet.disconnect();
					GodotAudioWorklet.worklet.port.onmessage = null;
					GodotAudioWorklet.worklet = null;
					GodotAudioWorklet.promise = null;
					resolve();
				}).catch(function (err) {
					// Aborted?
					GodotRuntime.error(err);
				});
			});
		},
	},

	godot_audio_worklet_create__proxy: 'sync',
	godot_audio_worklet_create__sig: 'ii',
	godot_audio_worklet_create: function (channels) {
		try {
			GodotAudioWorklet.create(channels);
		} catch (e) {
			GodotRuntime.error('Error starting AudioDriverWorklet', e);
			return 1;
		}
		return 0;
	},

	godot_audio_worklet_start__proxy: 'sync',
	godot_audio_worklet_start__sig: 'viiiii',
	godot_audio_worklet_start: function (
		p_in_buf,
		p_in_size,
		p_out_buf,
		p_out_size,
		p_state
	) {
		const out_buffer = GodotRuntime.heapSub(HEAPF32, p_out_buf, p_out_size);
		const in_buffer = GodotRuntime.heapSub(HEAPF32, p_in_buf, p_in_size);
		const state = GodotRuntime.heapSub(HEAP32, p_state, 4);
		GodotAudioWorklet.start(in_buffer, out_buffer, state);
	},

	godot_audio_worklet_start_no_threads__proxy: 'sync',
	godot_audio_worklet_start_no_threads__sig: 'viiiiii',
	godot_audio_worklet_start_no_threads: function (
		p_out_buf,
		p_out_size,
		p_out_callback,
		p_in_buf,
		p_in_size,
		p_in_callback
	) {
		const out_callback = GodotRuntime.get_func(p_out_callback);
		const in_callback = GodotRuntime.get_func(p_in_callback);
		GodotAudioWorklet.start_no_threads(
			p_out_buf,
			p_out_size,
			out_callback,
			p_in_buf,
			p_in_size,
			in_callback
		);
	},

	godot_audio_worklet_state_wait__sig: 'iiii',
	godot_audio_worklet_state_wait: function (
		p_state,
		p_idx,
		p_expected,
		p_timeout
	) {
		Atomics.wait(HEAP32, (p_state >> 2) + p_idx, p_expected, p_timeout);
		return Atomics.load(HEAP32, (p_state >> 2) + p_idx);
	},

	godot_audio_worklet_state_add__sig: 'iiii',
	godot_audio_worklet_state_add: function (p_state, p_idx, p_value) {
		return Atomics.add(HEAP32, (p_state >> 2) + p_idx, p_value);
	},

	godot_audio_worklet_state_get__sig: 'iii',
	godot_audio_worklet_state_get: function (p_state, p_idx) {
		return Atomics.load(HEAP32, (p_state >> 2) + p_idx);
	},
};

autoAddDeps(GodotAudioWorklet, '$GodotAudioWorklet');
mergeInto(LibraryManager.library, GodotAudioWorklet);

/*
 * The ScriptProcessorNode API, used as a fallback if AudioWorklet is not available.
 */
const GodotAudioScript = {
	$GodotAudioScript__deps: ['$GodotAudio'],
	$GodotAudioScript: {
		script: null,

		create: function (buffer_length, channel_count) {
			GodotAudioScript.script = GodotAudio.ctx.createScriptProcessor(
				buffer_length,
				2,
				channel_count
			);
			GodotAudio.driver = GodotAudioScript;
			return GodotAudioScript.script.bufferSize;
		},

		start: function (p_in_buf, p_in_size, p_out_buf, p_out_size, onprocess) {
			GodotAudioScript.script.onaudioprocess = function (event) {
				// Read input
				const inb = GodotRuntime.heapSub(HEAPF32, p_in_buf, p_in_size);
				const input = event.inputBuffer;
				if (GodotAudio.input) {
					const inlen = input.getChannelData(0).length;
					for (let ch = 0; ch < 2; ch++) {
						const data = input.getChannelData(ch);
						for (let s = 0; s < inlen; s++) {
							inb[s * 2 + ch] = data[s];
						}
					}
				}

				// Let Godot process the input/output.
				onprocess();

				// Write the output.
				const outb = GodotRuntime.heapSub(HEAPF32, p_out_buf, p_out_size);
				const output = event.outputBuffer;
				const channels = output.numberOfChannels;
				for (let ch = 0; ch < channels; ch++) {
					const data = output.getChannelData(ch);
					// Loop through samples and assign computed values.
					for (let sample = 0; sample < data.length; sample++) {
						data[sample] = outb[sample * channels + ch];
					}
				}
			};
			GodotAudioScript.script.connect(GodotAudio.ctx.destination);
		},

		get_node: function () {
			return GodotAudioScript.script;
		},

		close: function () {
			return new Promise(function (resolve, reject) {
				GodotAudioScript.script.disconnect();
				GodotAudioScript.script.onaudioprocess = function () {};
				GodotAudioScript.script = null;
				resolve();
			});
		},
	},

	godot_audio_script_create__proxy: 'sync',
	godot_audio_script_create__sig: 'iii',
	godot_audio_script_create: function (buffer_length, channel_count) {
		const buf_len = GodotRuntime.getHeapValue(buffer_length, 'i32');
		try {
			const out_len = GodotAudioScript.create(buf_len, channel_count);
			GodotRuntime.setHeapValue(buffer_length, out_len, 'i32');
		} catch (e) {
			GodotRuntime.error('Error starting AudioDriverScriptProcessor', e);
			return 1;
		}
		return 0;
	},

	godot_audio_script_start__proxy: 'sync',
	godot_audio_script_start__sig: 'viiiii',
	godot_audio_script_start: function (
		p_in_buf,
		p_in_size,
		p_out_buf,
		p_out_size,
		p_cb
	) {
		const onprocess = GodotRuntime.get_func(p_cb);
		GodotAudioScript.start(
			p_in_buf,
			p_in_size,
			p_out_buf,
			p_out_size,
			onprocess
		);
	},
};

autoAddDeps(GodotAudioScript, '$GodotAudioScript');
mergeInto(LibraryManager.library, GodotAudioScript);
