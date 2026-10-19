/**************************************************************************/
/*  main.cpp                                                              */
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

// Prototype: a WebGPU render thread on a pthread that owns the canvas (OffscreenCanvas), with
// its own device, driven by Atomics.waitAsync wake-ups from the main thread, yielding to its
// event loop after every frame so the frame presents and WebGPU callbacks run.
//
// Main thread: requestAnimationFrame loop; each tick burns LOGIC_MS of "game logic", then posts
// a frame to the render thread (atomic counter + notify) unless it is already 2 frames ahead.
// Render thread: on wake, burns RENDER_MS of "render CPU", clears the canvas to a colour that
// changes per frame, submits, returns to the event loop.
#include <emscripten.h>
#include <emscripten/atomic.h>
#include <emscripten/em_math.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <webgpu/webgpu.h>

static double LOGIC_MS = 4.0, RENDER_MS = 4.0;
static volatile uint32_t posted = 0; // frames posted by main
static volatile uint32_t rendered = 0; // frames finished by render thread
static uint32_t consumed = 0; // render-thread local

static WGPUInstance instance;
static WGPUDevice device;
static WGPUQueue queue;
static WGPUSurface surface;
static WGPUTextureFormat format;

static void burn(double ms) {
	double t = emscripten_get_now();
	while (emscripten_get_now() - t < ms) {
	}
}

static void wait_for_work(void);

static void render_one(void) {
	burn(RENDER_MS);
	WGPUSurfaceTexture st = {};
	wgpuSurfaceGetCurrentTexture(surface, &st);
	WGPUTextureView view = wgpuTextureCreateView(st.texture, NULL);
	WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, NULL);
	double c = (consumed % 120) / 120.0;
	WGPURenderPassColorAttachment ca = {};
	ca.view = view;
	ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	ca.loadOp = WGPULoadOp_Clear;
	ca.storeOp = WGPUStoreOp_Store;
	ca.clearValue = WGPUColor{ c, 0.3, 1.0 - c, 1.0 };
	WGPURenderPassDescriptor rp = {};
	rp.colorAttachmentCount = 1;
	rp.colorAttachments = &ca;
	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);
	WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, NULL);
	wgpuQueueSubmit(queue, 1, &cb);
	wgpuCommandBufferRelease(cb);
	wgpuCommandEncoderRelease(enc);
	wgpuTextureViewRelease(view);
	wgpuTextureRelease(st.texture);
	consumed++;
	rendered = consumed;
}

static void on_wake(int *address, uint32_t value, ATOMICS_WAIT_RESULT_T result, void *user) {
	// Render every frame posted so far, one task per frame so each presents, then wait again.
	if (consumed < posted) {
		render_one();
		if (consumed < posted) {
			emscripten_async_call((em_arg_callback_func)on_wake, NULL, 0); // next frame in a new task
			return;
		}
	}
	wait_for_work();
}

static void wait_for_work(void) {
	// Atomics.waitAsync on `posted`: resolves (on this thread's event loop) once main bumps it.
	// If work is already there, waitAsync returns "not-equal" at once and never calls back.
	if (posted != consumed) {
		emscripten_async_call((em_arg_callback_func)on_wake, NULL, 0);
		return;
	}
	ATOMICS_WAIT_TOKEN_T tok = emscripten_atomic_wait_async((int *)&posted, consumed, on_wake, NULL, EMSCRIPTEN_WAIT_ASYNC_INFINITY);
	if (!EMSCRIPTEN_IS_VALID_WAIT_TOKEN(tok)) {
		emscripten_async_call((em_arg_callback_func)on_wake, NULL, 0); // raced: changed in between
	}
}

// Called from JS (on the render thread) once its own device exists.
extern "C" EMSCRIPTEN_KEEPALIVE void render_thread_device_ready(void) {
	instance = wgpuCreateInstance(NULL);
	device = (WGPUDevice)EM_ASM_PTR({ return WebGPU.importJsDevice(Module["rtDevice"], $0); }, instance);
	queue = wgpuDeviceGetQueue(device);
	WGPUEmscriptenSurfaceSourceCanvasHTMLSelector sel = {};
	sel.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
	sel.selector = WGPUStringView{ "#canvas", WGPU_STRLEN };
	WGPUSurfaceDescriptor sd = {};
	sd.nextInChain = &sel.chain;
	surface = wgpuInstanceCreateSurface(instance, &sd);
	format = (WGPUTextureFormat)EM_ASM_INT({ var f = navigator.gpu.getPreferredCanvasFormat(); return f == 'bgra8unorm' ? 0x1B : 0x16; });
	WGPUSurfaceConfiguration cfg = {};
	cfg.device = device;
	cfg.format = format;
	cfg.usage = WGPUTextureUsage_RenderAttachment;
	cfg.width = 640;
	cfg.height = 360;
	cfg.presentMode = WGPUPresentMode_Fifo;
	cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
	wgpuSurfaceConfigure(surface, &cfg);
	EM_ASM({ console.log('[proto] render thread: device + surface ready on worker, isMainThread=' + (typeof window != = 'undefined')); });
	wait_for_work();
}

static void *render_thread_main(void *arg) {
	// Async device creation in this worker, then keep the worker alive on its event loop.
	EM_ASM({
		(async() = > {
			const a = await navigator.gpu.requestAdapter();
			Module["rtDevice"] = await a.requestDevice();
			_render_thread_device_ready();
		})();
	});
	emscripten_exit_with_live_runtime();
	return NULL;
}

static double t0 = 0;
static uint32_t r0 = 0;
static uint32_t ticks = 0;
static bool main_tick(double time, void *user) {
	burn(LOGIC_MS); // game logic
	if (posted - rendered < 2) {
		posted++;
		emscripten_atomic_notify((int *)&posted, 1);
	}
	ticks++;
	if (ticks == 120) {
		t0 = emscripten_get_now();
		r0 = rendered;
	} else if (ticks == 120 + 300) {
		double ms = (emscripten_get_now() - t0) / 300.0;
		double rms = (emscripten_get_now() - t0) / (double)(rendered - r0);
		EM_ASM({ console.log('[proto] RESULT main ' + $0.toFixed(2) + ' ms/tick, render thread ' + $1.toFixed(2) + ' ms/frame presented'); }, ms, rms);
	}
	return true;
}

int main(void) {
	LOGIC_MS = EM_ASM_DOUBLE({ return +(new URLSearchParams(location.search).get('logic') || 4); });
	RENDER_MS = EM_ASM_DOUBLE({ return +(new URLSearchParams(location.search).get('render') || 4); });
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	emscripten_pthread_attr_settransferredcanvases(&attr, "#canvas");
	pthread_t t;
	int rc = pthread_create(&t, &attr, render_thread_main, NULL);
	printf("[proto] pthread_create rc=%d logic=%.1fms render=%.1fms\n", rc, LOGIC_MS, RENDER_MS);
	emscripten_request_animation_frame_loop(main_tick, NULL);
	return 0;
}
