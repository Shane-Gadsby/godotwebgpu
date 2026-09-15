/**************************************************************************/
/*  native_emdawnwebgpu_repro.cpp                                         */
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

// Same repro as race_repro.js, but through the exact C API path the real
// engine uses: webgpu.h / emdawnwebgpu's JS glue (--use-port=emdawnwebgpu),
// not raw browser JS talking to navigator.gpu directly. race_repro.js
// already found no divergence at the raw-browser-JS level; this checks
// whether Emscripten's C API -> JS shim layer introduces a gap of its own
// that a pure-JS repro can't see, since that's the actual path
// rendering_device_driver_webgpu.cpp submits through (wgpuQueueSubmit(),
// not device.queue.submit() from JS).
//
// See webgpu_notes/TASKS.md Task 9.5 Round 41 / plan-of-attack.md Tier 1
// Phase 1 for context. Not Godot code — a standalone diagnostic.

#include <emscripten.h>
#include <webgpu/webgpu_cpp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static const wgpu::Instance instance = wgpuCreateInstance(nullptr);
static wgpu::Adapter adapter;
static wgpu::Device device;
static wgpu::Queue queue;

static const char kShaderSrc[] = R"(
struct Params {
    new_value: i32,
}

@group(0) @binding(0) var history_tex: texture_storage_2d<r32sint, read_write>;
@group(0) @binding(1) var average_tex: texture_storage_2d<r32sint, read_write>;
@group(0) @binding(2) var<uniform> params: Params;

@compute @workgroup_size(1)
fn main() {
    let pos = vec2<i32>(0, 0);
    let prev: i32 = textureLoad(history_tex, pos).x;
    var avg: i32 = textureLoad(average_tex, pos).x;
    avg = avg - prev + params.new_value;
    textureStore(history_tex, pos, vec4<i32>(params.new_value, 0, 0, 0));
    textureStore(average_tex, pos, vec4<i32>(avg, 0, 0, 0));
}
)";

static const char kFillerSrc[] = R"(
@group(0) @binding(0) var<storage, read_write> scratch: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    if (gid.x < arrayLength(&scratch)) {
        scratch[gid.x] = scratch[gid.x] * 1664525u + 1013904223u;
    }
}
)";

static const uint32_t kFillerElements = 4096;

static int32_t nextValue(int i) {
	return (int32_t)(((int64_t)i * 2654435761LL) % 998244353LL);
}

struct SlackResult {
	int slack;
	int calls;
	int32_t expected;
	int32_t actual;
};

static std::vector<SlackResult> g_results;
static std::vector<int> g_slackValues;
static int g_callsPerSlack = 500;
static size_t g_slackIndex = 0;

static wgpu::ComputePipeline g_pipeline;
static wgpu::ComputePipeline g_fillerPipeline;

static void runOneSlackSync(int slack, int calls, SlackResult *out) {
	wgpu::TextureDescriptor texDesc{};
	texDesc.size = { 1, 1, 1 };
	texDesc.format = wgpu::TextureFormat::R32Sint;
	texDesc.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::CopySrc;
	wgpu::Texture historyTex = device.CreateTexture(&texDesc);
	wgpu::Texture averageTex = device.CreateTexture(&texDesc);

	wgpu::BufferDescriptor paramsDesc{};
	paramsDesc.size = 16;
	paramsDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
	wgpu::Buffer paramsBuf = device.CreateBuffer(&paramsDesc);

	wgpu::BindGroupEntry entries[3]{};
	entries[0].binding = 0;
	entries[0].textureView = historyTex.CreateView();
	entries[1].binding = 1;
	entries[1].textureView = averageTex.CreateView();
	entries[2].binding = 2;
	entries[2].buffer = paramsBuf;
	entries[2].size = 16;

	wgpu::BindGroupDescriptor bgDesc{};
	bgDesc.layout = g_pipeline.GetBindGroupLayout(0);
	bgDesc.entryCount = 3;
	bgDesc.entries = entries;
	wgpu::BindGroup bindGroup = device.CreateBindGroup(&bgDesc);

	wgpu::BufferDescriptor scratchDesc{};
	scratchDesc.size = kFillerElements * 4;
	scratchDesc.usage = wgpu::BufferUsage::Storage;
	wgpu::Buffer scratchBuf = device.CreateBuffer(&scratchDesc);

	wgpu::BindGroupEntry fillerEntry{};
	fillerEntry.binding = 0;
	fillerEntry.buffer = scratchBuf;
	fillerEntry.size = kFillerElements * 4;

	wgpu::BindGroupDescriptor fillerBgDesc{};
	fillerBgDesc.layout = g_fillerPipeline.GetBindGroupLayout(0);
	fillerBgDesc.entryCount = 1;
	fillerBgDesc.entries = &fillerEntry;
	wgpu::BindGroup fillerBindGroup = device.CreateBindGroup(&fillerBgDesc);

	int32_t lastValue = 0;
	for (int i = 0; i < calls; i++) {
		lastValue = nextValue(i);
		int32_t paramsData[4] = { lastValue, 0, 0, 0 };
		queue.WriteBuffer(paramsBuf, 0, paramsData, sizeof(paramsData));

		{
			wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
			wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
			pass.SetPipeline(g_pipeline);
			pass.SetBindGroup(0, bindGroup);
			pass.DispatchWorkgroups(1);
			pass.End();
			wgpu::CommandBuffer cmd = encoder.Finish();
			queue.Submit(1, &cmd);
		}

		for (int s = 0; s < slack; s++) {
			wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
			wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
			pass.SetPipeline(g_fillerPipeline);
			pass.SetBindGroup(0, fillerBindGroup);
			pass.DispatchWorkgroups((kFillerElements + 63) / 64);
			pass.End();
			wgpu::CommandBuffer cmd = encoder.Finish();
			queue.Submit(1, &cmd);
		}
		// No explicit sync here — matches race_repro.js and the real
		// driver's own submission pattern.
	}

	out->slack = slack;
	out->calls = calls;
	out->expected = lastValue;
	out->actual = 0; // filled in by the async readback below

	wgpu::BufferDescriptor readbackDesc{};
	readbackDesc.size = 256;
	readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
	wgpu::Buffer readbackBuf = device.CreateBuffer(&readbackDesc);

	wgpu::CommandEncoder copyEncoder = device.CreateCommandEncoder();
	wgpu::TexelCopyTextureInfo src{};
	src.texture = averageTex;
	wgpu::TexelCopyBufferInfo dst{};
	dst.buffer = readbackBuf;
	dst.layout.bytesPerRow = 256;
	wgpu::Extent3D copySize = { 1, 1, 1 };
	copyEncoder.CopyTextureToBuffer(&src, &dst, &copySize);
	wgpu::CommandBuffer copyCmd = copyEncoder.Finish();
	queue.Submit(1, &copyCmd);

	// DEBUG: keep an explicit extra reference alive at global scope, to rule
	// out a lambda-capture lifetime bug as the cause of "buffer destroyed
	// before mapping resolved".
	static std::vector<wgpu::Buffer> g_keepAlive;
	g_keepAlive.push_back(readbackBuf);

	// Deliberately synchronous-looking readback via a raw pointer captured
	// by the callback below; runNextSlack() is invoked from inside the
	// MapAsync callback once the value is available (see below — this
	// whole program is callback-chained, not using Asyncify).
	readbackBuf.MapAsync(wgpu::MapMode::Read, 0, 256, wgpu::CallbackMode::AllowSpontaneous,
			[readbackBuf, out](wgpu::MapAsyncStatus status, wgpu::StringView message) mutable {
				if (status != wgpu::MapAsyncStatus::Success) {
					printf("RESULT_ERROR mapAsync failed status=%d msg=%.*s\n", (int)status, (int)message.length, message.data ? message.data : "");
					return;
				}
				const int32_t *data = (const int32_t *)readbackBuf.GetConstMappedRange(0, 256);
				out->actual = data[0];
				readbackBuf.Unmap();
				printf("SLACK_DONE slack=%d calls=%d expected=%d actual=%d\n", out->slack, out->calls, out->expected, out->actual);

				g_slackIndex++;
				extern void runNextSlack();
				runNextSlack();
			});
}

void runNextSlack() {
	if (g_slackIndex >= g_slackValues.size()) {
		printf("RESULT_DONE\n");
		bool anyDiverged = false;
		for (auto &r : g_results) {
			if (r.actual != r.expected) {
				anyDiverged = true;
			}
		}
		printf("RESULT_SUMMARY reproduced=%d\n", anyDiverged ? 1 : 0);
		return;
	}
	g_results.push_back(SlackResult{});
	runOneSlackSync(g_slackValues[g_slackIndex], g_callsPerSlack, &g_results.back());
}

static void onDeviceReady(wgpu::Device dev) {
	device = dev;
	queue = device.GetQueue();

	wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
	wgslDesc.code = kShaderSrc;
	wgpu::ShaderModuleDescriptor smDesc{};
	smDesc.nextInChain = &wgslDesc;
	wgpu::ShaderModule module = device.CreateShaderModule(&smDesc);

	wgpu::ComputePipelineDescriptor pipeDesc{};
	pipeDesc.compute.module = module;
	pipeDesc.compute.entryPoint = "main";
	g_pipeline = device.CreateComputePipeline(&pipeDesc);

	wgpu::ShaderModuleWGSLDescriptor fillerWgslDesc{};
	fillerWgslDesc.code = kFillerSrc;
	wgpu::ShaderModuleDescriptor fillerSmDesc{};
	fillerSmDesc.nextInChain = &fillerWgslDesc;
	wgpu::ShaderModule fillerModule = device.CreateShaderModule(&fillerSmDesc);

	wgpu::ComputePipelineDescriptor fillerPipeDesc{};
	fillerPipeDesc.compute.module = fillerModule;
	fillerPipeDesc.compute.entryPoint = "main";
	g_fillerPipeline = device.CreateComputePipeline(&fillerPipeDesc);

	printf("DEVICE_READY\n");
	runNextSlack();
}

static void onAdapterReady(wgpu::Adapter adp) {
	adapter = adp;
	wgpu::DeviceDescriptor desc{};
	desc.SetUncapturedErrorCallback([](const wgpu::Device &, wgpu::ErrorType type, wgpu::StringView message) {
		printf("DEVICE_ERROR type=%d msg=%.*s\n", (int)type, (int)message.length, message.data);
	});
	desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
			[](const wgpu::Device &, wgpu::DeviceLostReason reason, wgpu::StringView message) {
				printf("DEVICE_LOST reason=%d msg=%.*s\n", (int)reason, (int)message.length, message.data);
			});
	adapter.RequestDevice(&desc, wgpu::CallbackMode::AllowSpontaneous,
			[](wgpu::RequestDeviceStatus status, wgpu::Device dev, wgpu::StringView message) {
				if (status != wgpu::RequestDeviceStatus::Success) {
					printf("RESULT_ERROR RequestDevice failed: %.*s\n", (int)message.length, message.data);
					return;
				}
				onDeviceReady(dev);
			});
}

int main(int argc, char **argv) {
	// Config comes from the page's URL query string (?calls=N&slack=a,b,c),
	// read via a small JS shim, so run_native_repro.mjs doesn't need to
	// regenerate/edit the emcc-generated .html/.js on every rebuild.
	char callsBuf[32] = { 0 };
	char slackBuf[256] = { 0 };
	EM_ASM({
        const params = new URLSearchParams(location.search);
        const calls = params.get('calls') || '';
        const slack = params.get('slack') || '';
        stringToUTF8(calls, $0, 32);
        stringToUTF8(slack, $1, 256); }, callsBuf, slackBuf);

	if (callsBuf[0]) {
		g_callsPerSlack = atoi(callsBuf);
	}
	if (slackBuf[0]) {
		char *tok = strtok(slackBuf, ",");
		while (tok) {
			g_slackValues.push_back(atoi(tok));
			tok = strtok(nullptr, ",");
		}
	} else {
		g_slackValues = { 0, 1, 2, 4, 8, 16, 32 };
	}

	wgpu::RequestAdapterOptions options{};
	instance.RequestAdapter(&options, wgpu::CallbackMode::AllowSpontaneous,
			[](wgpu::RequestAdapterStatus status, wgpu::Adapter adp, wgpu::StringView message) {
				if (status != wgpu::RequestAdapterStatus::Success) {
					printf("RESULT_ERROR RequestAdapter failed: %.*s\n", (int)message.length, message.data);
					return;
				}
				onAdapterReady(adp);
			});

	return 0;
}
