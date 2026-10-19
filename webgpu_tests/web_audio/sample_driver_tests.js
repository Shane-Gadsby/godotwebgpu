/**
 * Parity tests for GodotSampleDriver (platform/web/js/libs/library_godot_audio.js), run in the page by
 * run_tests.mjs. Each test renders the real driver in an OfflineAudioContext and compares the output with
 * what AudioServer / AudioStreamPlaybackWAV do natively, using JS ports of the native algorithms below.
 */

const RATE = 48000;
const QUANTUM = 128;

/**
 * AudioStreamPlaybackWAV::mix()'s loop handling at a pitch of 1, sample by sample.
 * @return {!Float32Array}
 */
function nativeWavSequence(data, mode, begin, end, start, count) {
	const out = new Float32Array(count);
	const looping = mode !== 'disabled';
	let offset = start;
	let inc = mode === 'backward' ? -1 : 1;
	for (let k = 0; k < count; k++) {
		if (inc < 0) {
			if (looping && offset < begin) {
				if (mode === 'pingpong') {
					offset = begin + (begin - offset);
					inc = 1;
				} else {
					offset = end - (begin - offset);
				}
			} else if (offset < 0) {
				break;
			}
		} else if (looping && offset >= end) {
			if (mode === 'pingpong') {
				offset = end - (offset - end);
				inc = -1;
			} else {
				offset = begin + (offset - end);
			}
		} else if (offset >= data.length) {
			break;
		}
		out[k] = data[offset];
		offset += inc;
	}
	return out;
}

/** |H(f)| of AudioFilterSW's HIGHSHELF as AudioServer::_mix_step_for_channel() sets it up (resonance 1, 1 stage). */
function nativeHighshelfMagnitude(gain, cutoff, freq, rate) {
	const omega = 2 * Math.PI * cutoff / rate;
	const sin = Math.sin(omega);
	const cos = Math.cos(omega);
	const g = Math.max(gain, 0.001);
	const beta = Math.sqrt(g) / Math.sqrt(1);
	const a0 = (g + 1) - (g - 1) * cos + beta * sin;
	const b0 = g * ((g + 1) + (g - 1) * cos + beta * sin) / a0;
	const b1 = -2 * g * ((g - 1) + (g + 1) * cos) / a0;
	const b2 = g * ((g + 1) + (g - 1) * cos - beta * sin) / a0;
	const a1 = 2 * ((g - 1) - (g + 1) * cos) / a0; // AudioFilterSW stores a1/a2 negated; these are the textbook signs.
	const a2 = ((g + 1) - (g - 1) * cos - beta * sin) / a0;
	const w = 2 * Math.PI * freq / rate;
	const re = (c0, c1, c2) => c0 + c1 * Math.cos(-w) + c2 * Math.cos(-2 * w);
	const im = (c0, c1, c2) => c1 * Math.sin(-w) + c2 * Math.sin(-2 * w);
	const num = Math.hypot(re(b0, b1, b2), im(b0, b1, b2));
	const den = Math.hypot(re(1, a1, a2), im(1, a1, a2));
	return num / den;
}

/** AudioServer::move_bus() on a plain array. */
function nativeMoveBus(list, from, to) {
	const out = list.slice();
	const [bus] = out.splice(from, 1);
	if (to === -1) {
		out.push(bus);
	} else if (to < from) {
		out.splice(to, 0, bus);
	} else {
		out.splice(to - 1, 0, bus);
	}
	return out;
}

function volumes(...values) {
	const v = new Float32Array(8);
	v.set(values);
	return v;
}

/**
 * Renders the driver. `setup(driver, at)` configures it; `at(quantum, fn)` runs fn mid-render.
 * @return {!Promise<{out: !Array<!Float32Array>, driver: !Object, finished: !Array<number>, positions: !Object}>}
 */
async function render({ channels = 2, frames = RATE, buses = 1, setup }) {
	const ctx = new OfflineAudioContext(channels, frames, RATE);
	const driver = GodotSampleDriver(ctx);
	const finished = [];
	driver.setFinishedCallback((id) => finished.push(id));
	driver.setBusCount(buses);
	// One suspend() per instant: an OfflineAudioContext rejects a second one at the same frame.
	const pending = new Map();
	const at = (quantum, fn) => {
		if (!pending.has(quantum)) {
			pending.set(quantum, []);
			ctx.suspend((quantum * QUANTUM) / RATE).then(() => {
				pending.get(quantum).forEach((f) => f());
				ctx.resume();
			});
		}
		pending.get(quantum).push(fn);
	};
	setup(driver, at);
	const buffer = await ctx.startRendering();
	const out = [];
	for (let c = 0; c < channels; c++) {
		out.push(buffer.getChannelData(c));
	}
	return { out, driver, finished };
}

function constantStream(driver, id, left, right, frames = RATE, loopMode = 'forward') {
	driver.createStream(id, new Float32Array(frames).fill(left), new Float32Array(frames).fill(right), loopMode, 0, 0);
}

function play(driver, id, stream, { bus = 0, vol = volumes(1, 1), pitch = 1, offset = 0 } = {}) {
	driver.startPlayback(id, stream, offset, pitch, Int32Array.of(bus), vol);
}

const tests = [];
function test(name, fn) {
	tests.push({ name, fn });
}
function near(a, b, eps) {
	return Math.abs(a - b) <= eps;
}

test('stereo volume vector maps L/R gains to the source channels', async () => {
	const { out } = await render({
		setup(d) {
			constantStream(d, 1, 1, -1);
			play(d, 1, 1, { vol: volumes(0.5, 0.25) });
		},
	});
	const [l, r] = [out[0][1000], out[1][1000]];
	return { pass: near(l, 0.5, 1e-6) && near(r, -0.25, 1e-6), detail: `L ${l}, R ${r}` };
});

test('first frame plays at full volume, like a playback new to a bus in AudioServer', async () => {
	const { out } = await render({
		setup(d) {
			constantStream(d, 1, 1, 1);
			play(d, 1, 1, { vol: volumes(0.8, 0.8) });
		},
	});
	return { pass: near(out[0][0], 0.8, 1e-6), detail: `first sample ${out[0][0]}` };
});

for (const channels of [6, 8]) {
	test(`${channels}-channel output follows Godot's channel order`, async () => {
		const { out } = await render({
			channels,
			setup(d) {
				constantStream(d, 1, 1, -1);
				play(d, 1, 1, { vol: volumes(0.1, 0.2, 0.3, 0.7, 0.2, 0.4, 0.9, 0.6) });
			},
		});
		const expected = [0.1, -0.2, 0.3, -0.7, 0.2, -0.4, 0.9, -0.6].slice(0, channels);
		const got = out.map((ch) => ch[1000]);
		const pass = expected.every((e, i) => near(got[i], e, 1e-6));
		return { pass, detail: `got [${got.map((v) => v.toFixed(3))}]` };
	});
}

const LOOP_DATA_FRAMES = 200;
const LOOP_BEGIN = 50;
const LOOP_END = 150;
for (const mode of ['disabled', 'forward', 'pingpong', 'backward']) {
	for (const start of [0, 20, 70, 160]) {
		test(`${mode} loop from frame ${start} matches AudioStreamPlaybackWAV sample for sample`, async () => {
			const data = new Float32Array(LOOP_DATA_FRAMES).map((_, i) => (i + 1) / 1024);
			const count = 2000;
			const { out } = await render({
				frames: count,
				setup(d) {
					d.createStream(1, data, data, mode, LOOP_BEGIN, LOOP_END);
					play(d, 1, 1, { offset: start / RATE });
				},
			});
			const expected = nativeWavSequence(data, mode, LOOP_BEGIN, LOOP_END, start, count);
			let firstBad = -1;
			for (let i = 0; i < count; i++) {
				if (!near(out[0][i], expected[i], 1e-6)) {
					firstBad = i;
					break;
				}
			}
			const show = (a, i) => Array.from(a.subarray(Math.max(i - 2, 0), i + 3), (v) => Math.round(v * 1024 - 1)).join(',');
			return {
				pass: firstBad === -1,
				detail: firstBad === -1 ? `${count} frames identical` : `frame ${firstBad}: got [${show(out[0], firstBad)}], native [${show(expected, firstBad)}]`,
			};
		});
	}
}

test('non-looping playback reports finished once and is removed', async () => {
	const { finished, driver } = await render({
		setup(d) {
			constantStream(d, 1, 1, 1, 1000, 'disabled');
			play(d, 7, 1);
		},
	});
	return { pass: finished.length === 1 && finished[0] === 7 && driver.getPlayback(7) === null, detail: `finished ${JSON.stringify(finished)}` };
});

test('looping playback position, pitch and pause follow the native position', async () => {
	const S = 0.25 * RATE;
	const E = 0.75 * RATE;
	const t = (quantum) => quantum * QUANTUM / RATE;
	const wrap = (pos) => (pos >= 0.75 ? 0.25 + ((pos - 0.25) % 0.5) : pos);
	const results = [];
	await render({
		frames: RATE * 2,
		setup(d, at) {
			d.createStream(1, new Float32Array(RATE), new Float32Array(RATE), 'forward', S, E);
			play(d, 1, 1);
			const sample = (label, quantum, unwrapped) => at(quantum, () => {
				results.push({ label, got: d.getPlayback(1).getPlaybackPosition(), expected: wrap(unwrapped) });
			});
			sample('0.5 s at pitch 1', 188, t(188));
			sample('wrapped at 1.0 s', 375, t(375));
			at(375, () => d.getPlayback(1).setPitchScale(2));
			sample('0.1 s later at pitch 2', 413, t(375) + 2 * (t(413) - t(375)));
			at(450, () => d.getPlayback(1).pause(true));
			// The fade-out plays one more mix block (512 frames) before the source holds.
			sample('paused', 600, t(375) + 2 * (t(450) - t(375)) + 2 * 512 / RATE);
		},
	});
	const tolerance = 2 * QUANTUM / RATE * 2;
	const pass = results.length === 4 && results.every((r) => near(r.got, r.expected, tolerance));
	return { pass, detail: results.map((r) => `${r.label}: ${r.got.toFixed(4)} (native ${r.expected.toFixed(4)})`).join('; ') };
});

test('ping-pong and backward positions report the native (reversed) offset', async () => {
	const results = [];
	await render({
		frames: RATE,
		setup(d, at) {
			const silent = new Float32Array(RATE);
			d.createStream(1, silent, silent, 'pingpong', 0.25 * RATE, 0.75 * RATE);
			d.createStream(2, silent, silent, 'backward', 0, 0.75 * RATE);
			play(d, 1, 1);
			play(d, 2, 2);
			const time = 338 * QUANTUM / RATE; // About 0.9 s.
			at(338, () => {
				results.push({ label: 'ping-pong', got: d.getPlayback(1).getPlaybackPosition(), expected: 0.75 - (time - 0.75) });
				results.push({ label: 'backward', got: d.getPlayback(2).getPlaybackPosition(), expected: 0.75 - (time % 0.75) });
			});
		},
	});
	const pass = results.every((r) => near(r.got, r.expected, 0.006));
	return { pass, detail: results.map((r) => `${r.label}: ${r.got.toFixed(4)} (native ${r.expected.toFixed(4)})`).join('; ') };
});

test('volume changes ramp over about one mix block instead of stepping', async () => {
	const change = 100 * QUANTUM;
	const { out } = await render({
		setup(d, at) {
			constantStream(d, 1, 1, 1);
			play(d, 1, 1);
			at(100, () => d.getPlayback(1).setVolumes(Int32Array.of(0), volumes(0, 0)));
		},
	});
	const ch = out[0];
	let maxStep = 0;
	for (let i = change - 10; i < change + 1000; i++) {
		maxStep = Math.max(maxStep, Math.abs(ch[i] - ch[i - 1]));
	}
	const pass = near(ch[change - 1], 1, 1e-6) && maxStep < 0.02 && ch[change + 64] > 0.3 && ch[change + 640] < 0.02;
	return { pass, detail: `before ${ch[change - 1].toFixed(3)}, +64 ${ch[change + 64].toFixed(3)}, +640 ${ch[change + 640].toFixed(4)}, max step ${maxStep.toFixed(4)}` };
});

test('stop fades out over one mix block, then the source stops', async () => {
	const stop = 100 * QUANTUM;
	const { out, driver } = await render({
		setup(d, at) {
			constantStream(d, 1, 1, 1);
			play(d, 1, 1);
			at(100, () => d.removePlayback(1));
		},
	});
	const ch = out[0];
	let maxStep = 0;
	for (let i = stop - 10; i < stop + 1000; i++) {
		maxStep = Math.max(maxStep, Math.abs(ch[i] - ch[i - 1]));
	}
	const pass = ch[stop + 32] > 0.3 && ch[stop + 600] === 0 && maxStep < 0.05 && driver.getPlayback(1) === null;
	return { pass, detail: `+32 ${ch[stop + 32].toFixed(3)}, +600 ${ch[stop + 600]}, max step ${maxStep.toFixed(4)}` };
});

test('pause fades out and holds, resume fades back in', async () => {
	const { out } = await render({
		setup(d, at) {
			constantStream(d, 1, 1, 1);
			play(d, 1, 1);
			at(100, () => d.getPlayback(1).pause(true));
			at(200, () => d.getPlayback(1).pause(false));
		},
	});
	const ch = out[0];
	const pass = ch[150 * QUANTUM] < 0.001 && ch[200 * QUANTUM + 2000] > 0.99 && Math.abs(ch[100 * QUANTUM + 1] - ch[100 * QUANTUM]) < 0.05;
	return { pass, detail: `paused ${ch[150 * QUANTUM].toFixed(4)}, resumed ${ch[200 * QUANTUM + 2000].toFixed(4)}` };
});

test('restarting a playback fades the old voice out instead of cutting it', async () => {
	const restart = 100 * QUANTUM;
	const { out } = await render({
		setup(d, at) {
			const ramp = new Float32Array(RATE).map((_, i) => Math.sin(i / 20));
			d.createStream(1, ramp, ramp, 'forward', 0, 0);
			play(d, 1, 1);
			at(100, () => play(d, 1, 1));
		},
	});
	// The restarted voice begins at frame 0 of the stream (sin(0) = 0) while the old one fades: no jump.
	const step = Math.abs(out[0][restart] - out[0][restart - 1]);
	return { pass: step < 0.1, detail: `step at restart ${step.toFixed(4)}` };
});

test('3D attenuation high shelf matches AudioFilterSW above and at the cutoff', async () => {
	const results = [];
	for (const freq of [2000, 8000]) {
		const { out } = await render({
			setup(d) {
				const sine = new Float32Array(RATE).map((_, i) => Math.sin(2 * Math.PI * freq * i / RATE));
				d.createStream(1, sine, sine, 'forward', 0, 0);
				play(d, 1, 1);
				d.getPlayback(1).setHighshelf(0.25, 2000);
			},
		});
		let sum = 0;
		const from = RATE / 8;
		for (let i = from; i < out[0].length; i++) {
			sum += out[0][i] * out[0][i];
		}
		const measured = Math.sqrt(sum / (out[0].length - from)) * Math.SQRT2;
		const native = nativeHighshelfMagnitude(0.25, 2000, freq, RATE);
		results.push({ freq, measuredDb: 20 * Math.log10(measured), nativeDb: 20 * Math.log10(native) });
	}
	const pass = results.every((r) => near(r.measuredDb, r.nativeDb, 1.0));
	return { pass, detail: results.map((r) => `${r.freq} Hz: ${r.measuredDb.toFixed(2)} dB (native ${r.nativeDb.toFixed(2)} dB)`).join('; ') };
});

// Bus graph. Buses: 0 Master, then 1.., each with a distinct volume so the output identifies the path taken.
async function busOutput(buses, setup) {
	const { out } = await render({
		buses,
		setup(d) {
			constantStream(d, 1, 1, 1);
			setup(d);
		},
	});
	return out[0][1000];
}

test('a send to an earlier bus goes to Master, as in AudioServer::_mix_step()', async () => {
	const got = await busOutput(3, (d) => {
		d.setBusVolumeDb(1, -6.0206); // 0.5
		d.setBusVolumeDb(2, 0);
		d.setBusSend(2, 1); // Bus 2 -> bus 1 is backwards: Master instead.
		play(d, 1, 1, { bus: 2 });
	});
	return { pass: near(got, 1, 1e-4), detail: `got ${got.toFixed(4)}, native 1 (bus 1's 0.5 not applied)` };
});

test('a send to a later bus goes through it', async () => {
	const got = await busOutput(3, (d) => {
		d.setBusVolumeDb(2, -6.0206);
		d.setBusSend(1, 2);
		play(d, 1, 1, { bus: 1 });
	});
	return { pass: near(got, 0.5, 1e-4), detail: `got ${got.toFixed(4)}, native 0.5` };
});

test('solo keeps the soloed bus and its send chain, silences others, ignores mute', async () => {
	const got = await busOutput(4, (d) => {
		d.setBusSend(1, 2); // 1 -> 2 -> Master
		d.setBusSolo(1, true);
		d.setBusMute(1, true); // Ignored while soloing.
		d.createStream(2, new Float32Array(RATE).fill(0.25), new Float32Array(RATE).fill(0.25), 'forward', 0, 0);
		play(d, 1, 1, { bus: 1 }); // 1.0, soloed path
		play(d, 2, 2, { bus: 3 }); // 0.25, not soloed: silent
	});
	return { pass: near(got, 1, 1e-4), detail: `got ${got.toFixed(4)}, native 1` };
});

test('two soloed buses both play', async () => {
	const got = await busOutput(3, (d) => {
		d.setBusSolo(1, true);
		d.setBusSolo(2, true);
		play(d, 1, 1, { bus: 1 });
		play(d, 2, 1, { bus: 2 });
	});
	return { pass: near(got, 2, 1e-4), detail: `got ${got.toFixed(4)}, native 2` };
});

test('mute silences a bus without solo', async () => {
	const got = await busOutput(2, (d) => {
		d.setBusMute(1, true);
		play(d, 1, 1, { bus: 1 });
	});
	return { pass: near(got, 0, 1e-6), detail: `got ${got}` };
});

test('buses added by setBusCount send to Master', async () => {
	const got = await busOutput(4, (d) => {
		play(d, 1, 1, { bus: 3 });
	});
	return { pass: near(got, 1, 1e-6), detail: `got ${got}` };
});

for (const [from, to] of [[3, 1], [1, 3], [1, 4], [2, -1]]) {
	test(`moveBus(${from}, ${to}) reorders like AudioServer::move_bus()`, async () => {
		const count = 5;
		const gains = [1, 0.5, 0.25, 0.125, 0.0625];
		const order = nativeMoveBus([0, 1, 2, 3, 4], from, to);
		const got = [];
		for (let index = 1; index < count; index++) {
			got.push(await busOutput(count, (d) => {
				gains.forEach((g, i) => d.setBusVolumeDb(i, 20 * Math.log10(g)));
				d.moveBus(from, to);
				play(d, 1, 1, { bus: index });
			}));
		}
		const expected = order.slice(1).map((b) => gains[b]);
		const pass = expected.every((e, i) => near(got[i], e, 1e-4));
		return { pass, detail: `gains by index ${got.map((v) => v.toFixed(4))}, native ${expected}` };
	});
}

test('addBusAt inserts at the index AudioServer::add_bus() resolved', async () => {
	const got = await busOutput(3, (d) => {
		d.setBusVolumeDb(1, -6.0206);
		d.addBusAt(1); // New bus at 1, the 0.5 bus moves to 2.
		play(d, 1, 1, { bus: 2 });
	});
	return { pass: near(got, 0.5, 1e-4), detail: `got ${got.toFixed(4)}, native 0.5` };
});

test('removing a playback\'s bus and sending to it again fades and recovers', async () => {
	const { out } = await render({
		buses: 2,
		setup(d, at) {
			constantStream(d, 1, 1, 1);
			play(d, 1, 1, { bus: 1 });
			at(100, () => d.getPlayback(1).setVolumes(new Int32Array(0), new Float32Array(0)));
			at(200, () => d.getPlayback(1).setVolumes(Int32Array.of(1), volumes(1, 1)));
		},
	});
	const ch = out[0];
	const pass = ch[150 * QUANTUM] < 0.001 && ch[100 * QUANTUM + 16] > 0.5 && ch[200 * QUANTUM + 2000] > 0.99;
	return { pass, detail: `faded ${ch[150 * QUANTUM].toFixed(4)}, back ${ch[200 * QUANTUM + 2000].toFixed(4)}` };
});

// eslint-disable-next-line no-unused-vars
async function runTests(filter) {
	const results = [];
	for (const t of tests) {
		if (filter && !t.name.includes(filter)) {
			continue;
		}
		try {
			const r = await t.fn();
			results.push({ name: t.name, pass: r.pass, detail: r.detail });
		} catch (e) {
			results.push({ name: t.name, pass: false, detail: `threw: ${e && e.stack ? e.stack : e}` });
		}
	}
	return results;
}
