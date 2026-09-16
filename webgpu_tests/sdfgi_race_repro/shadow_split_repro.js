/**
 * Round 46 (Task 9.5 / plan-of-attack.md Tier 1 Phase 1) — RW-storage-texture
 * shadow-split repro. NOT Godot code, but modeling real driver code exactly.
 *
 * Code reading this round (rendering_device_driver_webgpu.cpp) found a
 * structural element no repro so far (race_repro.js, native_emdawnwebgpu_repro.cpp,
 * array_layer_repro.js, parallel_repro.js) has modeled: the real
 * lightprobe_history_texture/lightprobe_average_texture use rgba16sint/
 * rgba32sint, formats that need the 'readonly-and-readwrite-storage-textures'
 * WebGPU feature for real read_write storage access. That feature is
 * confirmed ABSENT on this session's real-GPU test adapter (Chrome/Vulkan/
 * nvidia/lovelace -- the same adapter every live SDFGI capture in this
 * investigation has used) via a direct feature-list check this round.
 *
 * Without that feature, the driver splits EVERY read_write storage texture
 * into TWO separate WGPUTextures: the original (write-only storage side,
 * used for imageStore) and a "shadow" (plain sampled texture, used for
 * imageLoad), kept in sync via an explicit
 * wgpuCommandEncoderCopyTextureToTexture(orig -> shadow) inserted right
 * before every command_bind_compute_uniform_sets() call
 * (rendering_device_driver_webgpu.cpp ~L10292, "refresh_rw_shadows"). A code
 * comment there documents this exact bug class already having silently
 * corrupted a DIFFERENT texture once (bokeh_dof.glsl's color_image going
 * solid black from a stale shadow) before the refresh-on-every-bind fix was
 * added. Both lightprobe_history_texture AND lightprobe_average_texture are
 * read_write, so BOTH get this split -- this repro models both pairs.
 *
 * Every prior repro read/wrote ONE unified storage texture directly
 * (texture_storage_2d<fmt, read_write>) -- confirmed correct behavior for
 * that shape at zero slack (race_repro.js/Round 40). This repro reproduces
 * the *exact same* 1-frame-gap telescoping accumulator
 * (avg -= prev; avg += ivalue; store both) but routes every read through a
 * shadow texture populated by a real GPU copyTextureToTexture from a
 * SEPARATE prior submission, exactly mirroring refresh_rw_shadows()'s
 * placement (copy issued in its own command encoder, right before the
 * compute pass that reads the shadow). If this diverges where the direct
 * read_write version doesn't, that isolates the shadow-copy mechanism
 * itself as the hazard site, not the RMW pattern per se.
 */

const WRITE_SHADER_SRC = /* wgsl */ `
struct Params {
    new_value: i32,
}

@group(0) @binding(0) var hist_w: texture_storage_2d<r32sint, write>;
@group(0) @binding(1) var hist_r: texture_2d<i32>;
@group(0) @binding(2) var avg_w: texture_storage_2d<r32sint, write>;
@group(0) @binding(3) var avg_r: texture_2d<i32>;
@group(0) @binding(4) var<uniform> params: Params;

@compute @workgroup_size(1)
fn main() {
    let pos = vec2<i32>(0, 0);
    let prev: i32 = textureLoad(hist_r, pos, 0).x;
    var avg: i32 = textureLoad(avg_r, pos, 0).x;
    avg = avg - prev + params.new_value;
    textureStore(hist_w, pos, vec4<i32>(params.new_value, 0, 0, 0));
    textureStore(avg_w, pos, vec4<i32>(avg, 0, 0, 0));
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

async function runOneSlack(device, pipeline, fillerPipeline, slack, calls) {
    const mkPair = () => {
        const orig = device.createTexture({
            size: [1, 1],
            format: 'r32sint',
            usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
        });
        const shadow = device.createTexture({
            size: [1, 1],
            format: 'r32sint',
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
        });
        return { orig, shadow };
    };
    const histPair = mkPair();
    const avgPair = mkPair();

    const paramsBuf = device.createBuffer({
        size: 16,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const bindGroup = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: histPair.orig.createView() },
            { binding: 1, resource: histPair.shadow.createView() },
            { binding: 2, resource: avgPair.orig.createView() },
            { binding: 3, resource: avgPair.shadow.createView() },
            { binding: 4, resource: { buffer: paramsBuf } },
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

    const samples = [];

    for (let i = 0; i < calls; i++) {
        device.queue.writeBuffer(paramsBuf, 0, new Int32Array([CONSTANT_VALUE, 0, 0, 0]));

        // Mirrors refresh_rw_shadows(): both shadows refreshed via a real GPU
        // copy, in their own command buffer, right before the compute pass
        // that reads them. The dependency on the PREVIOUS call's storage
        // write crosses a wgpuQueueSubmit() boundary here (except call 0).
        const copyEncoder = device.createCommandEncoder();
        copyEncoder.copyTextureToTexture({ texture: histPair.orig }, { texture: histPair.shadow }, [1, 1]);
        copyEncoder.copyTextureToTexture({ texture: avgPair.orig }, { texture: avgPair.shadow }, [1, 1]);
        device.queue.submit([copyEncoder.finish()]);

        const encoder = device.createCommandEncoder();
        const pass = encoder.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bindGroup);
        pass.dispatchWorkgroups(1);
        pass.end();
        device.queue.submit([encoder.finish()]);

        for (let s = 0; s < slack; s++) {
            const fillEncoder = device.createCommandEncoder();
            const fillPass = fillEncoder.beginComputePass();
            fillPass.setPipeline(fillerPipeline);
            fillPass.setBindGroup(0, fillerBindGroup);
            fillPass.dispatchWorkgroups(Math.ceil(FILLER_ELEMENTS / 64));
            fillPass.end();
            device.queue.submit([fillEncoder.finish()]);
        }
        // No explicit sync anywhere in this loop -- matches the real driver's
        // submission pattern exactly (refresh_rw_shadows also never waits).

        if ((i + 1) % 50 === 0 || i === calls - 1) {
            const readbackBuf = device.createBuffer({
                size: 256,
                usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
            });
            const rbEncoder = device.createCommandEncoder();
            rbEncoder.copyTextureToBuffer({ texture: avgPair.orig }, { buffer: readbackBuf, bytesPerRow: 256 }, [1, 1]);
            device.queue.submit([rbEncoder.finish()]);
            await device.queue.onSubmittedWorkDone();
            await readbackBuf.mapAsync(GPUMapMode.READ);
            const value = new Int32Array(readbackBuf.getMappedRange())[0];
            readbackBuf.unmap();
            readbackBuf.destroy();
            samples.push({ call: i + 1, value });
        }
    }

    histPair.orig.destroy();
    histPair.shadow.destroy();
    avgPair.orig.destroy();
    avgPair.shadow.destroy();

    // Correct (telescoping) behavior: after call 1, avg should hold exactly
    // CONSTANT_VALUE forever -- same expectation as race_repro.js's
    // known-good direct-storage 1-frame-gap case. Diverged if avg is ever
    // not CONSTANT_VALUE from the second sample onward (first sample allows
    // for the zero-initialized-shadow priming call).
    const relevant = samples.slice(1);
    const diverged = relevant.some((s) => s.value !== CONSTANT_VALUE);
    return { slack, calls, samples, diverged };
}

export async function runShadowSplitRace({ slacks = [0, 1, 2, 4, 8, 16], calls = 500 } = {}) {
    const { device, adapterInfo } = await initGPU();

    const shaderModule = device.createShaderModule({ code: WRITE_SHADER_SRC });
    const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module: shaderModule, entryPoint: 'main' } });

    const fillerModule = device.createShaderModule({ code: FILLER_SHADER_SRC });
    const fillerPipeline = device.createComputePipeline({ layout: 'auto', compute: { module: fillerModule, entryPoint: 'main' } });

    const results = [];
    for (const slack of slacks) {
        const r = await runOneSlack(device, pipeline, fillerPipeline, slack, calls);
        results.push(r);
    }

    const anyDiverged = results.some((r) => r.diverged);
    return { adapterInfo, results, reproduced: anyDiverged };
}
