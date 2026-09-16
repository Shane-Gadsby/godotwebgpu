/**
 * Round 46b (Task 9.5 / plan-of-attack.md Tier 1 Phase 1) — confirms the
 * exact bug found by code reading this round: rendering_device_driver_webgpu.cpp's
 * RWShadowRegistration/refresh_rw_shadows() and the texture_update() shadow-sync
 * path both used p_orig_tex->depth (always 1 for a 2D(-array) texture) instead
 * of the dimension-aware p_orig_tex->layers already used correctly by the
 * shadow texture's own *creation* code, when building the copyTextureToTexture
 * extent that refreshes a read_write-storage-texture's shadow. For a
 * multi-layer array texture (like sdfgi_integrate.glsl's 30-layer
 * lightprobe_history_texture, split into write+shadow because rgba16sint
 * needs a feature this session's real-GPU adapter lacks), that bug means
 * only layer 0 of the shadow ever gets refreshed -- every other layer's
 * shadow read returns stale/zero-initialized data forever, no matter what
 * gets written to the real texture. Fixed in
 * rendering_device_driver_webgpu.cpp this round (Task 9.5 Round 46).
 *
 * This repro isolates JUST that copy-extent bug (not the C++ driver, which
 * a JS/WebGPU program can't invoke directly) by running the exact same
 * array-layer telescoping-accumulator shape as array_layer_repro.js (Round
 * 44 -- confirmed correct through DIRECT read_write storage access) but
 * routed through a write+shadow split, with the shadow refresh extent
 * deliberately parameterized so both the buggy (depth=1) and fixed
 * (depth=historySize) behavior can be run side by side for direct
 * comparison against the same expected plateau.
 */

const WRITE_SHADER_SRC = /* wgsl */ `
struct Params {
    new_value: i32,
    layer: u32,
}

@group(0) @binding(0) var hist_w: texture_storage_2d_array<r32sint, write>;
@group(0) @binding(1) var hist_r: texture_2d_array<i32>;
@group(0) @binding(2) var avg_w: texture_storage_2d<r32sint, write>;
@group(0) @binding(3) var avg_r: texture_2d<i32>;
@group(0) @binding(4) var<uniform> params: Params;

@compute @workgroup_size(1)
fn main() {
    let pos = vec2<i32>(0, 0);
    let prev: i32 = textureLoad(hist_r, pos, i32(params.layer), 0).x;
    var avg: i32 = textureLoad(avg_r, pos, 0).x;
    avg = avg - prev + params.new_value;
    textureStore(hist_w, pos, i32(params.layer), vec4<i32>(params.new_value, 0, 0, 0));
    textureStore(avg_w, pos, vec4<i32>(avg, 0, 0, 0));
}
`;

const CONSTANT_VALUE = 512;

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

// buggyCopy: true reproduces the exact bug (copy extent depthOrArrayLayers=1
// regardless of historySize); false uses the fix (extent=historySize).
async function runOneConfig(device, pipeline, historySize, cyclesToRun, buggyCopy) {
    const calls = historySize * cyclesToRun;

    const histOrig = device.createTexture({
        size: [1, 1, historySize],
        format: 'r32sint',
        dimension: '2d',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    const histShadow = device.createTexture({
        size: [1, 1, historySize],
        format: 'r32sint',
        dimension: '2d',
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });
    const avgOrig = device.createTexture({
        size: [1, 1],
        format: 'r32sint',
        usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    const avgShadow = device.createTexture({
        size: [1, 1],
        format: 'r32sint',
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });
    const paramsBuf = device.createBuffer({
        size: 16,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    const bindGroup = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: histOrig.createView({ dimension: '2d-array' }) },
            { binding: 1, resource: histShadow.createView({ dimension: '2d-array' }) },
            { binding: 2, resource: avgOrig.createView() },
            { binding: 3, resource: avgShadow.createView() },
            { binding: 4, resource: { buffer: paramsBuf } },
        ],
    });

    // The exact bug under test: buggy uses depthOrArrayLayers=1 (matching
    // p_orig_tex->depth, always 1 for a 2D-array texture); fixed uses
    // historySize (matching p_orig_tex->layers).
    const histCopyDepth = buggyCopy ? 1 : historySize;

    const cycleSamples = [];
    const expectedPlateau = CONSTANT_VALUE * historySize;

    for (let i = 0; i < calls; i++) {
        const layer = i % historySize;
        device.queue.writeBuffer(paramsBuf, 0, new Int32Array([CONSTANT_VALUE, layer, 0, 0]));

        // Mirrors refresh_rw_shadows(): both shadows refreshed via a real GPU
        // copy right before the compute pass that reads them.
        const copyEncoder = device.createCommandEncoder();
        copyEncoder.copyTextureToTexture({ texture: histOrig }, { texture: histShadow }, [1, 1, histCopyDepth]);
        copyEncoder.copyTextureToTexture({ texture: avgOrig }, { texture: avgShadow }, [1, 1]);
        device.queue.submit([copyEncoder.finish()]);

        const encoder = device.createCommandEncoder();
        const pass = encoder.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(0, bindGroup);
        pass.dispatchWorkgroups(1);
        pass.end();
        device.queue.submit([encoder.finish()]);

        if ((i + 1) % historySize === 0) {
            const readbackBuf = device.createBuffer({
                size: 256,
                usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
            });
            const rbEncoder = device.createCommandEncoder();
            rbEncoder.copyTextureToBuffer({ texture: avgOrig }, { buffer: readbackBuf, bytesPerRow: 256 }, [1, 1]);
            device.queue.submit([rbEncoder.finish()]);
            await device.queue.onSubmittedWorkDone();
            await readbackBuf.mapAsync(GPUMapMode.READ);
            const value = new Int32Array(readbackBuf.getMappedRange())[0];
            readbackBuf.unmap();
            readbackBuf.destroy();
            cycleSamples.push({ call: i + 1, cycle: (i + 1) / historySize, average: value });
        }
    }

    histOrig.destroy();
    histShadow.destroy();
    avgOrig.destroy();
    avgShadow.destroy();

    const lastFew = cycleSamples.slice(-3).map((s) => s.average);
    const converged = lastFew.length >= 2 && lastFew.every((v) => v === expectedPlateau);

    return { buggyCopy, historySize, calls, expectedPlateau, cycleSamples, converged, diverged: !converged };
}

export async function runArrayShadowSplitRace({ historySizes = [4, 30], cyclesToRun = 10 } = {}) {
    const { device, adapterInfo } = await initGPU();

    const shaderModule = device.createShaderModule({ code: WRITE_SHADER_SRC });
    const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module: shaderModule, entryPoint: 'main' } });

    const results = [];
    for (const historySize of historySizes) {
        results.push(await runOneConfig(device, pipeline, historySize, cyclesToRun, /* buggyCopy */ true));
        results.push(await runOneConfig(device, pipeline, historySize, cyclesToRun, /* buggyCopy */ false));
    }

    // We EXPECT the buggy configs to diverge and the fixed configs to
    // converge -- that combination is what confirms the theory, not a
    // generic "any divergence" flag like other repros in this directory.
    const buggyAllDiverged = results.filter((r) => r.buggyCopy).every((r) => r.diverged);
    const fixedAllConverged = results.filter((r) => !r.buggyCopy).every((r) => !r.diverged);
    return { adapterInfo, results, theoryConfirmed: buggyAllDiverged && fixedAllConverged };
}
