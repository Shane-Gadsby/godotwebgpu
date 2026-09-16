/**
 * Round 45 (Task 9.5 / plan-of-attack.md Tier 1 Phase 1) — per-dispatch
 * parallelism repro. NOT Godot code.
 *
 * Every prior repro in this directory (race_repro.js, native_emdawnwebgpu_repro.cpp,
 * array_layer_repro.js) dispatches exactly one invocation per call
 * (`dispatchWorkgroups(1)`, `@workgroup_size(1)`) — zero concurrent traffic on
 * the shared textures. The real sdfgi_integrate.glsl shader dispatches
 * thousands of concurrent invocations per call, all touching the *same two*
 * persistent textures (at different, non-overlapping sub-addresses)
 * simultaneously. This repro matches that dispatch shape and addressing
 * scheme exactly (see plan-of-attack.md Round 45 spec for the derivation of
 * every number below) to test whether concurrency itself is the missing
 * ingredient no prior single-invocation repro could have exposed.
 *
 * Real shader shape (sdfgi_integrate.glsl / gi.cpp), replicated here:
 *   - workgroup_size(8, 8, 1), dispatched (37, 3, 1) -> 7104 invocations,
 *     matching dispatch_threads(289, 17, 1)'s out-of-bounds tail exactly
 *     (bounds-checked in-shader, not a smaller exact-fit grid).
 *   - each in-bounds invocation loops SH_SIZE=16 rows of a shared
 *     289 x (17*16=272) texture, touching history_tex[pos.x, row, layer]
 *     and average_tex[pos.x, row] per iteration.
 *   - history_tex is a texture_storage_2d_array<r32sint, read_write>,
 *     average_tex a texture_storage_2d<r32sint, read_write>.
 *
 * Pass/fail: identical derivation to array_layer_repro.js (a zero-initialized
 * ring buffer fed a constant value V per call reaches a plateau of
 * V * history_size after one full ring-buffer cycle and holds flat every
 * cycle after) but checked across EVERY one of the 289x272 independent
 * texels, not just one — a hazard that only affects a subset under
 * concurrent load would be invisible to a single-texel check.
 */

const GRID_X = 289; // probe_axis_count^2 for probe_axis_count=17
const GRID_Y = 17; // rows-of-16 count -> texture height 17*16=272
const SH_SIZE = 16;
const TEX_W = GRID_X; // 289
const TEX_H = GRID_Y * SH_SIZE; // 272
const DISPATCH_X = 37; // ceil(289/8)
const DISPATCH_Y = 3; // ceil(17/8)

const SHADER_SRC = /* wgsl */ `
struct Params {
    new_value: i32,
    layer: u32,
}

@group(0) @binding(0) var history_tex: texture_storage_2d_array<r32sint, read_write>;
@group(0) @binding(1) var average_tex: texture_storage_2d<r32sint, read_write>;
@group(0) @binding(2) var<uniform> params: Params;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    if (gid.x >= ${GRID_X}u || gid.y >= ${GRID_Y}u) { return; }
    let pos = vec2<i32>(i32(gid.x), 0);
    for (var i: i32 = 0; i < ${SH_SIZE}; i = i + 1) {
        let row = i32(gid.y) * ${SH_SIZE} + i;
        let history_pos = vec2<i32>(pos.x, row);
        let avg_pos = vec2<i32>(pos.x, row);
        let prev = textureLoad(history_tex, history_pos, i32(params.layer)).x;
        var avg = textureLoad(average_tex, avg_pos).x;
        avg = avg - prev + params.new_value;
        textureStore(history_tex, history_pos, i32(params.layer), vec4<i32>(params.new_value, 0, 0, 0));
        textureStore(average_tex, avg_pos, vec4<i32>(avg, 0, 0, 0));
    }
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
const STRIDE = 8; // per-cycle sampling stride (full grid only on final cycle)

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

// Reads back average_tex, checking either every texel (full=true) or a
// strided subset, against the expected plateau. Returns list of mismatches
// (capped) rather than throwing, so a partial-grid hazard is visible.
async function checkAverageTex(device, averageTex, expected, full) {
    const bytesPerRow = Math.ceil((TEX_W * 4) / 256) * 256;
    const bufSize = bytesPerRow * TEX_H;
    const readbackBuf = device.createBuffer({
        size: bufSize,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    const encoder = device.createCommandEncoder();
    encoder.copyTextureToBuffer(
        { texture: averageTex },
        { buffer: readbackBuf, bytesPerRow, rowsPerImage: TEX_H },
        [TEX_W, TEX_H],
    );
    device.queue.submit([encoder.finish()]);
    await device.queue.onSubmittedWorkDone();
    await readbackBuf.mapAsync(GPUMapMode.READ);
    const data = new Int32Array(readbackBuf.getMappedRange().slice(0));
    readbackBuf.unmap();
    readbackBuf.destroy();

    const stride = full ? 1 : STRIDE;
    const mismatches = [];
    for (let y = 0; y < TEX_H; y += stride) {
        for (let x = 0; x < TEX_W; x += stride) {
            const idx = (y * bytesPerRow) / 4 + x;
            const value = data[idx];
            if (value !== expected) {
                mismatches.push({ x, y, value, expected });
                if (mismatches.length >= 20) return { checked: 'partial-cap', mismatches };
            }
        }
    }
    return { checked: full ? 'full' : 'strided', mismatches };
}

async function runOneConfig(device, pipeline, fillerPipeline, historySize, filler, cyclesToRun) {
    const calls = historySize * cyclesToRun;

    const historyTex = device.createTexture({
        size: [TEX_W, TEX_H, historySize],
        format: 'r32sint',
        dimension: '2d',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    const averageTex = device.createTexture({
        size: [TEX_W, TEX_H],
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

    const cycleSamples = [];
    const expectedPlateau = CONSTANT_VALUE * historySize;

    for (let i = 0; i < calls; i++) {
        const layer = i % historySize;
        device.queue.writeBuffer(paramsBuf, 0, new Int32Array([CONSTANT_VALUE, layer, 0, 0]));

        const encoder = device.createCommandEncoder();
        const pass = encoder.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bindGroup);
        pass.dispatchWorkgroups(DISPATCH_X, DISPATCH_Y, 1);
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
        // No explicit sync between the main dispatch and filler dispatches —
        // matches the real driver's submission pattern.

        if ((i + 1) % historySize === 0) {
            const cycle = (i + 1) / historySize;
            const isFinalCycle = cycle === cyclesToRun;
            const { checked, mismatches } = await checkAverageTex(device, averageTex, expectedPlateau, isFinalCycle);
            cycleSamples.push({ call: i + 1, cycle, checked, mismatchCount: mismatches.length, sampleMismatches: mismatches.slice(0, 5) });
        }
    }

    historyTex.destroy();
    averageTex.destroy();

    const anyMismatch = cycleSamples.some((s) => s.mismatchCount > 0);
    const lastTwo = cycleSamples.slice(-2);
    const stillGrowing = lastTwo.length === 2 && lastTwo[0].mismatchCount !== lastTwo[1].mismatchCount;

    return { historySize, filler, calls, expectedPlateau, cycleSamples, diverged: anyMismatch, stillGrowing };
}

export async function runParallelRace({ historySizes = [30], fillers = [0, 2, 8], cyclesToRun = 3 } = {}) {
    const { device, adapterInfo } = await initGPU();

    const shaderModule = device.createShaderModule({ code: SHADER_SRC });
    const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module: shaderModule, entryPoint: 'main' } });

    const fillerModule = device.createShaderModule({ code: FILLER_SHADER_SRC });
    const fillerPipeline = device.createComputePipeline({ layout: 'auto', compute: { module: fillerModule, entryPoint: 'main' } });

    const results = [];
    for (const historySize of historySizes) {
        for (const filler of fillers) {
            const r = await runOneConfig(device, pipeline, fillerPipeline, historySize, filler, cyclesToRun);
            results.push(r);
        }
    }

    const anyDiverged = results.some((r) => r.diverged);
    return { adapterInfo, gridShape: { GRID_X, GRID_Y, TEX_W, TEX_H, DISPATCH_X, DISPATCH_Y }, results, reproduced: anyDiverged };
}
