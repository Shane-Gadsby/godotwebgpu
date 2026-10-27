/**************************************************************************/
/*  library_godot_webgpu.js                                                */
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

// JavaScript the WebGPU driver and the render thread run goes through here, never
// through an EM_ASM body that names anything of Emscripten's (the port's `$WebGPU`,
// `UTF8ToString`, `_free`, `wasmTable`, `GodotConfig`, ...). A release template is
// closure-compiled, which renames all of those, and an EM_ASM body:
// - in a regular template is part of the glue, but reads `WebGPU["importJsDevice"]`
//   by its old property name ("M.importJsDevice is not a function");
// - in a dlink (GDExtension) template lives in the side module and is eval'd at load
//   time, where the renamed names do not exist ("stringToNewUTF8 is not defined").
// A library function's `__deps` are renamed with it. EM_ASM bodies may still use
// `Module['...']`, browser APIs and their own arguments. (webgpu_notes/TASKS.md §3 trap 74)
const GodotWebGPU = {
	$GodotWebGPU__deps: ['$GodotRuntime'],
	$GodotWebGPU: {},

	// Wraps the GPUDevice engine.js created (Module.preinitializedWebGPUDevice) in a
	// WGPUDevice handle owned by `p_instance`, or returns 0 when there is none.
	godot_webgpu_import_device__deps: ['$WebGPU'],
	godot_webgpu_import_device__sig: 'pp',
	godot_webgpu_import_device: function (p_instance) {
		const device = Module['preinitializedWebGPUDevice'];
		if (!device) {
			return 0;
		}
		return WebGPU.importJsDevice(device, p_instance);
	},

	// Publishes `globalThis.godotWebGPUShaderStats` (see _publish_shader_stats() in
	// rendering_device_driver_webgpu.cpp) on the main thread, where pages and test
	// harnesses read it. Frees `p_names`, a malloc'd newline-joined list.
	godot_webgpu_shader_stats_publish__proxy: 'async',
	godot_webgpu_shader_stats_publish__sig: 'viiiiipdiddddd',
	godot_webgpu_shader_stats_publish: function (p_baked, p_precompiled, p_cached, p_translated, p_specialized, p_names,
			p_container_create_ms, p_container_create_calls, p_container_stage_loop_ms,
			p_containers, p_spirv_bytes, p_wgsl_bytes, p_footer_parse_ms) {
		const names = GodotRuntime.parseString(p_names);
		GodotRuntime.free(p_names);
		globalThis['godotWebGPUShaderStats'] = {
			'baked': p_baked,
			'precompiled': p_precompiled,
			'cached': p_cached,
			'translated': p_translated,
			'specialized': p_specialized,
			'translatedShaders': names.length ? names.split('\n') : [],
			'containerCreateMs': p_container_create_ms,
			'containerCreateCalls': p_container_create_calls,
			'containerStageLoopMs': p_container_stage_loop_ms,
			'containersParsed': p_containers,
			'spirvBytes': p_spirv_bytes,
			'wgslBytes': p_wgsl_bytes,
			'footerParseMs': p_footer_parse_ms,
		};
	},

	// The features and limits of the main thread's device as malloc'd JSON, for the
	// render thread's device (DisplayServerWeb::render_thread_prestart()). Also points
	// Module.canvas, through which Emscripten resolves "#canvas", at the shell's canvas.
	godot_webgpu_render_thread_prestart__deps: ['$GodotConfig'],
	godot_webgpu_render_thread_prestart__sig: 'p',
	godot_webgpu_render_thread_prestart: function () {
		const d = Module['preinitializedWebGPUDevice'];
		const limits = {};
		if (d) {
			for (const k in d.limits) {
				limits[k] = d.limits[k];
			}
		}
		Module['canvas'] = GodotConfig.canvas;
		return GodotRuntime.allocString(JSON.stringify({ 'features': d ? Array.from(d.features) : [], 'limits': limits }));
	},

	// On the render thread: creates its device from godot_webgpu_render_thread_prestart()'s
	// JSON, stores it as this worker's Module.preinitializedWebGPUDevice and calls
	// `p_ready` (void(int): 1 on success, 0 on failure) on this thread.
	godot_webgpu_render_thread_request_device__sig: 'vpp',
	godot_webgpu_render_thread_request_device: function (p_desc, p_ready) {
		const desc = JSON.parse(GodotRuntime.parseString(p_desc));
		const ready = GodotRuntime.get_func(p_ready);
		(async () => {
			const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
			const features = desc['features'].filter((f) => adapter.features.has(f));
			const limits = {};
			for (const k in desc['limits']) {
				if (adapter.limits[k] !== undefined) {
					limits[k] = desc['limits'][k];
				}
			}
			const device = await adapter.requestDevice({ requiredFeatures: features, requiredLimits: limits });
			device.lost.then((info) => {
				if (info.reason === 'destroyed') {
					return; // Our own device.destroy() at shutdown (_render_thread_shutdown).
				}
				console.error(`[Godot] WebGPU device lost on the render thread (reason: ${info.reason}): ${info.message}`);
				globalThis['GODOT_WEBGPU_DEVICE_LOST'] = true;
			});
			device.addEventListener('uncapturederror', (event) => {
				const error = event.error;
				const kind = (error && error.constructor && error.constructor.name) || 'UnknownError';
				console.error(`[Godot] WebGPU uncaptured error: ${kind}: ${error && error.message ? error.message : String(error)}`);
			});
			// This worker's own Module: RenderingContextDriverWebGPU::initialize() imports it from here.
			Module['preinitializedWebGPUDevice'] = device;
			ready(1);
		})().catch((e) => {
			console.error(`[Godot] WebGPU: the render thread could not create its device: ${e}`);
			ready(0);
		});
	},

	// On the render thread: calls `p_wake` (void(void *), with null) as a new task, after
	// the frame just drawn presents. A MessageChannel message, not a timer: Chrome clamps
	// nested setTimeout(0) to 4 ms.
	godot_webgpu_render_thread_yield__sig: 'vp',
	godot_webgpu_render_thread_yield: function (p_wake) {
		let port = globalThis['godotRenderThreadYield'];
		if (!port) {
			const channel = new MessageChannel();
			const wake = GodotRuntime.get_func(p_wake);
			channel.port1.onmessage = () => wake(0);
			port = globalThis['godotRenderThreadYield'] = channel.port2;
		}
		port.postMessage(0);
	},
};

autoAddDeps(GodotWebGPU, '$GodotWebGPU');
mergeInto(LibraryManager.library, GodotWebGPU);
