// Injected into the page before any export script runs (Playwright addInitScript).
//
// Task 14 subtask 1.1: bracket each post-download startup phase of a WebGPU web
// export. Everything here is external monkey-patching -- deliberately so, because
// it profiles an *already-exported* project without needing an engine rebuild,
// and so it keeps working against exports made by other builds of this fork.
//
// The one phase that cannot be patched from outside is the inside of callMain():
// on WebGPU the engine's first frame runs synchronously inside it (see Task 14),
// so JS cannot observe anything during that window except the WebGPU API calls
// the engine itself makes. That is exactly why the WebGPU-call accounting below
// exists: time spent inside the callMain window that is NOT in a WebGPU call is,
// by subtraction, CPU work -- resource decode, scene parse, GDScript -- which is
// the part of the load this task still has no numbers for.
(function () {
	if (window.__godotPhases) { return; }

	const P = {
		t0: performance.now(),
		marks: {},
		events: [],
		wasm: [],           // {api, label, startMs, durationMs, mime, ok}
		fetches: [],        // {url, startMs, durationMs, bytes}
		shaderModules: [],  // {label, startMs, durationMs}
		renderPipelines: [],
		computePipelines: [],
		gpuWrites: [],      // {kind, startMs, durationMs, bytes}
		gpuCreates: [],     // {kind, startMs, durationMs}
		consoleTimes: [],   // {startMs, text}
		frames: [],         // rAF timestamps, to spot the first painted frames
		eagerKicked: 0,
		eagerDone: 0,
		eagerFailed: 0,
	};
	window.__godotPhases = P;

	const now = () => performance.now() - P.t0;
	P.mark = function (name, extra) {
		if (!(name in P.marks)) { P.marks[name] = now(); }
		P.events.push(Object.assign({ name: name, atMs: now() }, extra || {}));
	};
	P.mark('instrument_installed');

	// ---- Engine-dispatched events (already shipped by this fork, see Task 14) ----
	for (const name of ['godot-before-callmain', 'godot-webgpu-first-frame']) {
		window.addEventListener(name, () => P.mark(name));
	}
	window.addEventListener('godot-webgpu-shader-compile', (e) => {
		P.events.push({ name: 'godot-webgpu-shader-compile', atMs: now(), misses: e.detail && e.detail.misses });
		P.marks['godot-webgpu-shader-compile_first'] = P.marks['godot-webgpu-shader-compile_first'] ?? now();
		P.marks['godot-webgpu-shader-compile_last'] = now();
	});

	// ---- Document lifecycle ----
	document.addEventListener('DOMContentLoaded', () => P.mark('DOMContentLoaded'));
	window.addEventListener('load', () => P.mark('window_load'));

	// ---- WASM compile/instantiate ----
	// Subtask 2.1 asks whether instantiateStreaming is really taken; record which
	// API was used and the MIME type it was handed, since a wrong Content-Type is
	// what silently demotes streaming to the arrayBuffer() path.
	function wrapWasm(apiName) {
		const orig = WebAssembly[apiName];
		if (typeof orig !== 'function') { return; }
		WebAssembly[apiName] = function (source, imports) {
			const start = now();
			const entry = { api: apiName, startMs: start, durationMs: null, mime: null, bytes: null };
			P.wasm.push(entry);
			const record = (ok) => { entry.durationMs = now() - start; entry.ok = ok; };
			try {
				const resolved = Promise.resolve(source);
				if (apiName.endsWith('Streaming')) {
					// Peek at the Response without consuming it.
					resolved.then((res) => {
						try {
							entry.mime = res && res.headers ? res.headers.get('content-type') : null;
							const len = res && res.headers ? res.headers.get('content-length') : null;
							entry.bytes = len ? parseInt(len, 10) : null;
						} catch (_) { /* opaque response */ }
					}, () => {});
				} else if (source && typeof source.byteLength === 'number') {
					entry.bytes = source.byteLength;
				}
				const out = orig.call(WebAssembly, source, imports);
				return Promise.resolve(out).then(
					(v) => { record(true); return v; },
					(e) => { record(false); throw e; }
				);
			} catch (e) {
				record(false);
				throw e;
			}
		};
	}
	['instantiateStreaming', 'compileStreaming', 'instantiate', 'compile'].forEach(wrapWasm);

	// ---- Asset downloads ----
	// Only the big ones matter (.wasm/.pck/.side.wasm); everything else is noise.
	const origFetch = window.fetch;
	if (typeof origFetch === 'function') {
		window.fetch = function (input, init) {
			const url = String((input && input.url) || input || '');
			const start = now();
			return origFetch.call(window, input, init).then((res) => {
				const entry = { url: url, startMs: start, headersMs: now() - start, durationMs: null, bytes: null, status: res.status, mime: res.headers.get('content-type') };
				P.fetches.push(entry);
				// The body is consumed by the caller, not us -- infer completion from
				// content-length plus the clone's arrayBuffer only for small files, so
				// we never double-download the 46MB wasm.
				const len = res.headers.get('content-length');
				entry.bytes = len ? parseInt(len, 10) : null;
				return res;
			});
		};
	}

	// ---- WebGPU device acquisition + per-call accounting ----
	function timeCall(bucket, label, fn, extra) {
		const start = now();
		const out = fn();
		bucket.push(Object.assign({ label: label, startMs: start, durationMs: now() - start }, extra || {}));
		return out;
	}

	function patchDevice(device) {
		if (!device || device.__godotPhasesPatched) { return device; }
		device.__godotPhasesPatched = true;

		const origCSM = device.createShaderModule.bind(device);
		device.createShaderModule = (desc) => timeCall(P.shaderModules, (desc && desc.label) || '', () => origCSM(desc),
			{ codeChars: desc && desc.code ? desc.code.length : 0 });

		const origCRP = device.createRenderPipeline.bind(device);
		device.createRenderPipeline = (desc) => {
			const pipe = timeCall(P.renderPipelines, (desc && desc.label) || '', () => origCRP(desc));
			if (window.__eagerPipelines && device.createRenderPipelineAsync) {
				P.eagerKicked++;
				device.createRenderPipelineAsync(desc).then(() => { P.eagerDone++; }, () => { P.eagerFailed++; });
			}
			return pipe;
		};

		const origCCP = device.createComputePipeline.bind(device);
		device.createComputePipeline = (desc) => {
			const pipe = timeCall(P.computePipelines, (desc && desc.label) || '', () => origCCP(desc));
			// Diagnostic for Task 14 subtask 2: also kick off an *async* creation of
			// the same pipeline, without awaiting it. Dawn compiles lazily on first
			// use for a synchronous create, which is why the compile cost surfaces
			// much later as a blocking queue operation; an async create is supposed
			// to start compiling immediately. If firing these makes that later block
			// shrink, then moving the driver to the async entry points would move
			// compilation off the critical path.
			if (window.__eagerPipelines && device.createComputePipelineAsync) {
				P.eagerKicked++;
				device.createComputePipelineAsync(desc).then(() => { P.eagerDone++; }, () => { P.eagerFailed++; });
			}
			return pipe;
		};

		const origCT = device.createTexture.bind(device);
		device.createTexture = (desc) => timeCall(P.gpuCreates, 'texture', () => origCT(desc));

		const origCB = device.createBuffer.bind(device);
		device.createBuffer = (desc) => timeCall(P.gpuCreates, 'buffer', () => origCB(desc),
			{ bytes: desc && desc.size });

		const queue = device.queue;
		if (queue && !queue.__godotPhasesPatched) {
			queue.__godotPhasesPatched = true;
			const origWB = queue.writeBuffer.bind(queue);
			queue.writeBuffer = function (buf, off, data, dOff, size) {
				return timeCall(P.gpuWrites, 'writeBuffer', () => origWB(buf, off, data, dOff, size),
					{ bytes: size ?? (data && data.byteLength) ?? 0 });
			};
			const origWT = queue.writeTexture.bind(queue);
			queue.writeTexture = function (dst, data, layout, size) {
				// Record the destination, because exactly one writeTexture per load
				// blocks for ~1s while the other ~1400 cost microseconds -- naming
				// that one texture is what distinguishes "this upload is expensive"
				// from "this call is where the wire happens to flush".
				return timeCall(P.gpuWrites, 'writeTexture', () => origWT(dst, data, layout, size), {
					bytes: (data && data.byteLength) || 0,
					dstLabel: (dst && dst.texture && dst.texture.label) || '',
					dstFormat: (dst && dst.texture && dst.texture.format) || '',
					dstSize: dst && dst.texture ? `${dst.texture.width}x${dst.texture.height}` : '',
					writeSize: size ? `${size.width || size[0]}x${size.height || size[1]}` : '',
					submitsBefore: P.gpuWrites.filter((x) => x.label === 'submit').length,
				});
			};
			const origSubmit = queue.submit.bind(queue);
			queue.submit = function (buffers) {
				return timeCall(P.gpuWrites, 'submit', () => origSubmit(buffers),
					{ bytes: 0, count: buffers ? buffers.length : 0 });
			};
		}
		return device;
	}

	if (navigator.gpu) {
		const origRA = navigator.gpu.requestAdapter.bind(navigator.gpu);
		navigator.gpu.requestAdapter = function (opts) {
			P.mark('requestAdapter_start');
			return origRA(opts).then((adapter) => {
				P.mark('requestAdapter_done');
				if (adapter) {
					const origRD = adapter.requestDevice.bind(adapter);
					adapter.requestDevice = function (dOpts) {
						P.mark('requestDevice_start');
						return origRD(dOpts).then((device) => {
							P.mark('requestDevice_done');
							return patchDevice(device);
						});
					};
				}
				return adapter;
			});
		};
	}

	// ---- Frame observation ----
	// The callMain window blocks rAF entirely; the gap in this series is therefore
	// an independent confirmation of the stall's length, measured without relying
	// on the engine's own events.
	let frameCount = 0;
	(function tick() {
		P.frames.push(now());
		if (++frameCount < 20000) { requestAnimationFrame(tick); }
	}());
}());
