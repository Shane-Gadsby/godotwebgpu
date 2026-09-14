/**
 * Minimal, from-scratch repro of the SDFGI brightness-runaway pattern —
 * NOT Godot code. See webgpu_notes/TASKS.md Task 9.5 (Round 38-39) and
 * plan-of-attack.md Tier 1 Phase 1 for the full investigation this narrows.
 *
 * The essential pattern from sdfgi_integrate.glsl (~L339-355):
 *   ivec4 prev_value = imageLoad(lightprobe_history_texture, prev_pos);
 *   ivec4 average     = imageLoad(lightprobe_average_texture, average_pos);
 *   average -= prev_value;
 *   average += ivalue;
 *   imageStore(lightprobe_history_texture, prev_pos, ivalue);
 *   imageStore(lightprobe_average_texture, average_pos, average);
 * — two persistent read_write storage textures, same texel read-modified-written
 * every call, across separate wgpuQueueSubmit()s with no explicit sync.
 *
 * Algebraically (telescoping): if every call correctly reads the previous
 * call's own write, "average" after N calls always equals just the most
 * recent value v_N (each call's subtraction exactly cancels the prior
 * call's addition). That's the whole point of this repro: the "correct"
 * final value is a single known integer, not a running sum, so ANY
 * cross-submission hazard-tracking gap (a call reading a stale/torn
 * history_tex or average_tex value) shows up as a hard integer mismatch,
 * not a fuzzy statistical difference — and a race that only sometimes
 * drops the subtraction produces exactly the monotonic-growth shape SDFGI
 * shows on WebGPU: correct arithmetic that just doesn't cancel the way
 * the algorithm depends on it doing.
 */

const SHADER_SRC = /* wgsl */ `
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
`;

// Unrelated filler work submitted between accumulate calls to simulate a
// real frame's other GPU work sharing the same queue timeline — without it,
// "slack" would just mean idle time, not the actual scenario (other render
// passes queued in between two touches of the same persistent texture).
const FILLER_SHADER_SRC = /* wgsl */ `
@group(0) @binding(0) var<storage, read_write> scratch: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    if (gid.x < arrayLength(&scratch)) {
        scratch[gid.x] = scratch[gid.x] * 1664525u + 1013904223u;
    }
}
`;

const FILLER_ELEMENTS = 4096;

function nextValue(i) {
    // Deterministic, non-trivial sequence — avoids accidental cancellation
    // patterns a simple i or i*2 sequence might hide.
    return ((i * 2654435761) % 998244353) | 0;
}

async function initGPU() {
    if (!navigator.gpu) {
        throw new Error('WebGPU not available (navigator.gpu is undefined)');
    }
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
        throw new Error('requestAdapter() returned null');
    }
    const device = await adapter.requestDevice();
    const info = adapter.info || {};
    return { device, adapterInfo: { vendor: info.vendor, architecture: info.architecture, device: info.device, description: info.description } };
}

async function runOneSlack(device, pipeline, fillerPipeline, slack, calls) {
    const historyTex = device.createTexture({
        size: [1, 1],
        format: 'r32sint',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    const averageTex = device.createTexture({
        size: [1, 1],
        format: 'r32sint',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    const paramsBuf = device.createBuffer({
        size: 16, // min uniform buffer binding alignment
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const bindGroup = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: historyTex.createView() },
            { binding: 1, resource: averageTex.createView() },
            { binding: 2, resource: { buffer: paramsBuf } },
        ],
    });

    const scratchBuf = device.createBuffer({
        size: FILLER_ELEMENTS * 4,
        usage: GPUBufferUsage.STORAGE,
    });
    const fillerBindGroup = device.createBindGroup({
        layout: fillerPipeline.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: { buffer: scratchBuf } }],
    });

    let lastValue = 0;
    for (let i = 0; i < calls; i++) {
        lastValue = nextValue(i);
        device.queue.writeBuffer(paramsBuf, 0, new Int32Array([lastValue, 0, 0, 0]));

        const encoder = device.createCommandEncoder();
        const pass = encoder.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bindGroup);
        pass.dispatchWorkgroups(1);
        pass.end();
        device.queue.submit([encoder.finish()]);

        // Filler submits: unrelated GPU work, same queue, no sync — this is
        // the "slack" (other frame work happening between touches of the
        // same persistent texture, matching Round 39's methodology).
        for (let s = 0; s < slack; s++) {
            const fillEncoder = device.createCommandEncoder();
            const fillPass = fillEncoder.beginComputePass();
            fillPass.setPipeline(fillerPipeline);
            fillPass.setBindGroup(0, fillerBindGroup);
            fillPass.dispatchWorkgroups(Math.ceil(FILLER_ELEMENTS / 64));
            fillPass.end();
            device.queue.submit([fillEncoder.finish()]);
        }
        // Deliberately no device.queue.onSubmittedWorkDone() here — the
        // hypothesis under test is specifically about relying on WebGPU's
        // own automatic hazard tracking with no explicit CPU-side sync,
        // matching how Godot's own render loop submits frames.
    }

    // Final explicit sync, purely to read the result back for comparison.
    const readbackBuf = device.createBuffer({
        size: 256, // texture-to-buffer copy row alignment minimum
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    const copyEncoder = device.createCommandEncoder();
    copyEncoder.copyTextureToBuffer(
        { texture: averageTex },
        { buffer: readbackBuf, bytesPerRow: 256 },
        [1, 1],
    );
    device.queue.submit([copyEncoder.finish()]);
    await device.queue.onSubmittedWorkDone();
    await readbackBuf.mapAsync(GPUMapMode.READ);
    const actual = new Int32Array(readbackBuf.getMappedRange())[0];
    readbackBuf.unmap();

    historyTex.destroy();
    averageTex.destroy();

    return { slack, calls, expected: lastValue, actual, diverged: actual !== lastValue };
}

export async function runRace({ slackValues = [0, 1, 2, 4, 8, 16, 32], callsPerSlack = 500 } = {}) {
    const { device, adapterInfo } = await initGPU();

    const shaderModule = device.createShaderModule({ code: SHADER_SRC });
    const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module: shaderModule, entryPoint: 'main' } });

    const fillerModule = device.createShaderModule({ code: FILLER_SHADER_SRC });
    const fillerPipeline = device.createComputePipeline({ layout: 'auto', compute: { module: fillerModule, entryPoint: 'main' } });

    const results = [];
    for (const slack of slackValues) {
        const r = await runOneSlack(device, pipeline, fillerPipeline, slack, callsPerSlack);
        results.push(r);
    }

    const anyDiverged = results.some((r) => r.diverged);
    return { adapterInfo, results, reproduced: anyDiverged };
}
