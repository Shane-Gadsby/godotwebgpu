/**
 * In-page half of the specialization-constant override test.
 *
 * Runs the same shader module twice through createComputePipelineAsync() — once
 * with no pipeline constants (so every override keeps its default) and once with
 * real values for @id(0)/@id(1)/@id(2) — and reports what the shader wrote both
 * times. This is the WebGPU-API-level equivalent of what
 * RenderingDeviceDriverWebGPU::compute_pipeline_create() does on the override
 * path, against the WGSL the real preprocessing pipeline produces.
 */

// Matches chained_spec_ops.comp: group 0 binding 0 is the output buffer,
// group 3 binding 120 is the push-constant ring-buffer slot this driver
// rewrites push constants into.
const OUTPUT_GROUP = 0;
const OUTPUT_BINDING = 0;
const PC_GROUP = 3;
const PC_BINDING = 120;
const VALUES_PER_INVOCATION = 4;

async function runOverrideTest(wgsl) {
    if (!navigator.gpu) {
        return { ok: false, error: 'navigator.gpu is undefined — no WebGPU in this browser' };
    }
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
        return { ok: false, error: 'requestAdapter() returned null — no GPU adapter' };
    }
    const device = await adapter.requestDevice();
    // Reported so a run makes clear whether it exercised real hardware or a
    // software adapter (see scene_smoketest/run_scenes.mjs on why that matters).
    const info = adapter.info || {};
    const adapterInfo = [info.vendor, info.architecture, info.description].filter(Boolean).join(' / ') || 'unknown';

    const errors = [];
    device.addEventListener('uncapturederror', (e) => errors.push(String(e.error.message)));

    const module = device.createShaderModule({ code: wgsl });
    const compilationInfo = await module.getCompilationInfo();
    const compileErrors = compilationInfo.messages.filter((m) => m.type === 'error').map((m) => m.message);
    if (compileErrors.length > 0) {
        return { ok: false, error: 'shader module failed to compile', compileErrors, adapterInfo };
    }

    const outputLayout = device.createBindGroupLayout({
        entries: [{
            binding: OUTPUT_BINDING,
            visibility: GPUShaderStage.COMPUTE,
            buffer: { type: 'storage' },
        }],
    });
    const pcLayout = device.createBindGroupLayout({
        entries: [{
            binding: PC_BINDING,
            visibility: GPUShaderStage.COMPUTE,
            buffer: { type: 'read-only-storage' },
        }],
    });
    const empty = device.createBindGroupLayout({ entries: [] });
    // Groups 1 and 2 are unused by this shader but a pipeline layout has to be
    // dense up to the highest group index it declares.
    const pipelineLayout = device.createPipelineLayout({
        bindGroupLayouts: [outputLayout, empty, empty, pcLayout],
    });

    const outputBuffer = device.createBuffer({
        size: VALUES_PER_INVOCATION * 4,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
    });
    const readbackBuffer = device.createBuffer({
        size: VALUES_PER_INVOCATION * 4,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    // PushConstants { index : u32 } — index 0, so the shader writes values[0..3].
    const pcBuffer = device.createBuffer({
        size: 4,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    device.queue.writeBuffer(pcBuffer, 0, new Uint32Array([0]));

    const outputBindGroup = device.createBindGroup({
        layout: outputLayout,
        entries: [{ binding: OUTPUT_BINDING, resource: { buffer: outputBuffer } }],
    });
    const emptyBindGroup = device.createBindGroup({ layout: empty, entries: [] });
    const pcBindGroup = device.createBindGroup({
        layout: pcLayout,
        entries: [{ binding: PC_BINDING, resource: { buffer: pcBuffer } }],
    });

    async function dispatchWith(constants) {
        const pipeline = await device.createComputePipelineAsync({
            layout: pipelineLayout,
            compute: { module, entryPoint: 'main', constants },
        });
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginComputePass();
        pass.setPipeline(pipeline);
        pass.setBindGroup(OUTPUT_GROUP, outputBindGroup);
        pass.setBindGroup(1, emptyBindGroup);
        pass.setBindGroup(2, emptyBindGroup);
        pass.setBindGroup(PC_GROUP, pcBindGroup);
        pass.dispatchWorkgroups(1);
        pass.end();
        encoder.copyBufferToBuffer(outputBuffer, 0, readbackBuffer, 0, VALUES_PER_INVOCATION * 4);
        device.queue.submit([encoder.finish()]);
        await readbackBuffer.mapAsync(GPUMapMode.READ);
        const values = Array.from(new Uint32Array(readbackBuffer.getMappedRange().slice(0)));
        readbackBuffer.unmap();
        return values;
    }

    let defaults;
    let overridden;
    try {
        defaults = await dispatchWith({});
        // The same call shape the driver builds: string keys of the numeric @id.
        overridden = await dispatchWith({ '0': 100, '1': 200, '2': 5 });
    } catch (e) {
        return { ok: false, error: `pipeline creation or dispatch failed: ${e}`, errors, adapterInfo };
    }

    await device.queue.onSubmittedWorkDone();
    return { ok: true, defaults, overridden, errors, adapterInfo };
}

window.runOverrideTest = runOverrideTest;
