/**
 * Follow-up to race_repro.js, testing the specific shape Task 9.5 Round 43
 * points at (see webgpu_notes/TASKS.md) — NOT Godot code.
 *
 * Round 43's live instrumentation against the real SDFGI accumulator found
 * that once ivalue (the raw per-frame light contribution) settled to a
 * constant value, average kept growing at a rate matching ivalue almost
 * exactly, long after lightprobe_history_tex's 30-layer ring buffer should
 * have made prev_value also read back that same constant and caused
 * convergence. That points at a DIFFERENT access pattern than race_repro.js
 * tested: lightprobe_history_tex is a texture ARRAY (history_size layers),
 * written once per call at a ROTATING layer index, and read back at that
 * SAME layer only history_size calls later — maximal slack, not minimal.
 * race_repro.js's history_tex was a flat 1-layer texture with a 1-call gap.
 *
 * This repro matches sdfgi_integrate.glsl's real access pattern exactly:
 *   ivec3 prev_pos = ivec3(pos.x, pos.y * SH_SIZE + i, history_index);
 *   ivec4 prev_value = imageLoad(lightprobe_history_texture, prev_pos);
 *   ivec4 average = imageLoad(lightprobe_average_texture, average_pos);
 *   average -= prev_value; average += ivalue;
 *   imageStore(lightprobe_history_texture, prev_pos, ivalue);
 *   imageStore(lightprobe_average_texture, average_pos, average);
 * — history_index = render_pass % history_size, i.e. writes rotate through
 * history_size layers and each layer is re-touched (read+write) only once
 * every history_size calls.
 *
 * With a CONSTANT per-call value V written from a zero-initialized texture,
 * correct behavior is a SLIDING-WINDOW SUM, not a single-value telescope:
 * during the ring buffer's first fill-up cycle every prev_value read is
 * still 0 (never written), so average climbs linearly to V*history_size by
 * the end of cycle 1 — then, once every layer holds V, each subsequent
 * call's prev_value also reads V, so average's net change per call is
 * V - V = 0: it should hold flat at V*history_size forever after. (An
 * earlier version of this repro's own comment/expectation incorrectly
 * assumed convergence to plain V, as in race_repro.js's simpler 1-frame-gap
 * case — that assumption doesn't hold once the ring buffer needs an entire
 * cycle to prime from empty. Fixed after the first run caught it.) Growth
 * that continues PAST the first cycle's flat plateau is what would match
 * Round 43's live finding — this repro's pass/fail check is against that
 * corrected expectation.
 */

const SHADER_SRC = /* wgsl */ `
struct Params {
    new_value: i32,
    layer: u32,
}

@group(0) @binding(0) var history_tex: texture_storage_2d_array<r32sint, read_write>;
@group(0) @binding(1) var average_tex: texture_storage_2d<r32sint, read_write>;
@group(0) @binding(2) var<uniform> params: Params;

@compute @workgroup_size(1)
fn main() {
    let pos = vec2<i32>(0, 0);
    let prev: i32 = textureLoad(history_tex, pos, i32(params.layer)).x;
    var avg: i32 = textureLoad(average_tex, pos).x;
    avg = avg - prev + params.new_value;
    textureStore(history_tex, pos, i32(params.layer), vec4<i32>(params.new_value, 0, 0, 0));
    textureStore(average_tex, pos, vec4<i32>(avg, 0, 0, 0));
}
`;

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
const CONSTANT_VALUE = 512; // matches Round 43's observed steady-state ivalue

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

async function runOneHistorySize(device, pipeline, fillerPipeline, historySize, filler, cyclesToRun) {
    const calls = historySize * cyclesToRun;

    const historyTex = device.createTexture({
        size: [1, 1, historySize],
        format: 'r32sint',
        dimension: '2d',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    const averageTex = device.createTexture({
        size: [1, 1],
        format: 'r32sint',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    const paramsBuf = device.createBuffer({
        size: 16,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const bindGroup = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: historyTex.createView({ dimension: '2d-array' }) },
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

    // Sample average's value at the end of each ring-buffer cycle, to see
    // the trend (converge vs. keep growing) rather than just a final number.
    const cycleSamples = [];

    for (let i = 0; i < calls; i++) {
        const layer = i % historySize;
        device.queue.writeBuffer(paramsBuf, 0, new Int32Array([CONSTANT_VALUE, layer, 0, 0]));

        const encoder = device.createCommandEncoder();
        const pass = encoder.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bindGroup);
        pass.dispatchWorkgroups(1);
        pass.end();
        device.queue.submit([encoder.finish()]);

        for (let s = 0; s < filler; s++) {
            const fillEncoder = device.createCommandEncoder();
            const fillPass = fillEncoder.beginComputePass();
            fillPass.setPipeline(fillerPipeline);
            fillPass.setBindGroup(0, fillerBindGroup);
            fillPass.dispatchWorkgroups(Math.ceil(FILLER_ELEMENTS / 64));
            fillPass.end();
            device.queue.submit([fillEncoder.finish()]);
        }
        // No explicit sync — matches the real driver's submission pattern.

        if ((i + 1) % historySize === 0) {
            // Sync + readback once per cycle (cheap relative to total calls)
            // to build a trend without forcing a flush every single call.
            const readbackBuf = device.createBuffer({
                size: 256,
                usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
            });
            const copyEncoder = device.createCommandEncoder();
            copyEncoder.copyTextureToBuffer({ texture: averageTex }, { buffer: readbackBuf, bytesPerRow: 256 }, [1, 1]);
            device.queue.submit([copyEncoder.finish()]);
            await device.queue.onSubmittedWorkDone();
            await readbackBuf.mapAsync(GPUMapMode.READ);
            const value = new Int32Array(readbackBuf.getMappedRange())[0];
            readbackBuf.unmap();
            cycleSamples.push({ call: i + 1, cycle: (i + 1) / historySize, average: value });
        }
    }

    historyTex.destroy();
    averageTex.destroy();

    // Correct behavior: average reaches CONSTANT_VALUE * historySize by the
    // end of cycle 1 (the ring buffer's fill-up sliding-window sum) and
    // stays flat every cycle after — NOT plain CONSTANT_VALUE (see file
    // header comment for the derivation). Diverged if it keeps changing
    // past cycle 1, or never reaches the expected plateau at all.
    const expectedPlateau = CONSTANT_VALUE * historySize;
    const lastFew = cycleSamples.slice(-3).map((s) => s.average);
    const converged = lastFew.length >= 2 && lastFew.every((v) => v === expectedPlateau);
    const stillGrowing = cycleSamples.length >= 2 &&
        cycleSamples[cycleSamples.length - 1].average !== cycleSamples[cycleSamples.length - 2].average;

    return { historySize, filler, calls, expectedPlateau, cycleSamples, converged, stillGrowing, diverged: !converged };
}

export async function runArrayLayerRace({ historySizes = [4, 30], filler = 2, cyclesToRun = 10 } = {}) {
    const { device, adapterInfo } = await initGPU();

    const shaderModule = device.createShaderModule({ code: SHADER_SRC });
    const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module: shaderModule, entryPoint: 'main' } });

    const fillerModule = device.createShaderModule({ code: FILLER_SHADER_SRC });
    const fillerPipeline = device.createComputePipeline({ layout: 'auto', compute: { module: fillerModule, entryPoint: 'main' } });

    const results = [];
    for (const historySize of historySizes) {
        const r = await runOneHistorySize(device, pipeline, fillerPipeline, historySize, filler, cyclesToRun);
        results.push(r);
    }

    const anyDiverged = results.some((r) => r.diverged);
    return { adapterInfo, results, reproduced: anyDiverged };
}
