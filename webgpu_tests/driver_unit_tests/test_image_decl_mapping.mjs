/**
 * Tests for the mapping from raw SPIR-V image declarations to the WebGPU facts
 * a BindGroupLayout entry needs.
 *
 * The walk that produces those declarations lives in
 * drivers/webgpu/spirv_lite_reflect.cpp and is tested against real SPIR-V by
 * webgpu_tests/preprocessing_tests (Test 22, via tint_convert_cli
 * --image-decls). What is mirrored here is the other half:
 * _pre_dce_image_info_from_decls() in rendering_device_driver_webgpu.cpp, which
 * turns those declarations into a (set << 16 | binding*2) -> {format, dim,
 * sampleType} map. That half stays in the driver because it speaks WebGPU
 * enums, which the editor-side baker has no access to.
 *
 * The binding doubling is the part most worth pinning down. These declarations
 * are read from raw SPIR-V, so they carry the original GLSL binding number,
 * while every other populator of the same maps reads @binding(B) out of Tint's
 * output -- i.e. after split_combined_samplers() has doubled it. Keying these
 * undoubled would silently put them in a different key space, where they would
 * never be found and the BGL entry would fall back to a default format.
 */

import { describe, it, assert } from './test_harness.mjs';

// --- Simulate _spirv_image_format_to_wgpu() for the formats under test ---
// SPIR-V ImageFormat operand -> WGPUTextureFormat name. 0 (Unknown) is what a
// sampled (non-storage) texture declares, and maps to Undefined.
const SPIRV_IMAGE_FORMAT = {
    0: 'Undefined', // Unknown
    2: 'RGBA16Float', // Rgba16f
    3: 'R32Float', // R32f
    15: 'R8Unorm', // R8
    24: 'R32Sint', // R32i
    33: 'R32Uint', // R32ui
};

function spirvImageFormatToWgpu(spvFormat) {
    return SPIRV_IMAGE_FORMAT[spvFormat] ?? 'Undefined';
}

// --- Simulate _pre_dce_image_info_from_decls() ---
// Mirrors the C++: SPIR-V Dim is 0=1D, 1=2D, 2=3D, 3=Cube, and `arrayed`
// selects the array flavor for the two dimensions that have one. A declaration
// that resolves to nothing usable is dropped rather than inserted, so it cannot
// shadow a later post-Tint scan that does find real evidence.
function preDceImageInfoFromDecls(decls) {
    const result = new Map();
    for (const d of decls) {
        const format = spirvImageFormatToWgpu(d.spvFormat);
        let dim;
        switch (d.spvDim) {
            case 0: dim = '1D'; break;
            case 1: dim = d.arrayed ? '2DArray' : '2D'; break;
            case 2: dim = '3D'; break;
            case 3: dim = d.arrayed ? 'CubeArray' : 'Cube'; break;
            default: dim = 'Undefined'; break;
        }
        // Only an integer component type is recovered: Float is already the
        // right default wherever sampleType is consulted.
        let sampleType = 'Undefined';
        if (d.intSignedness === 1) {
            sampleType = 'Sint';
        } else if (d.intSignedness === 0) {
            sampleType = 'Uint';
        }

        if (format === 'Undefined' && dim === 'Undefined' && sampleType === 'Undefined') {
            continue;
        }
        // The doubling: see this file's header comment.
        result.set((d.set << 16) | (d.binding * 2), { format, dim, sampleType });
    }
    return result;
}

function decl(set, binding, spvFormat, spvDim, arrayed = 0, intSignedness = -1) {
    return { set, binding, spvFormat, spvDim, arrayed, intSignedness };
}

export function runTests() {
    describe('Image declaration mapping: binding key space', () => {
        it('should double the GLSL binding to match the post-split WGSL binding', () => {
            const m = preDceImageInfoFromDecls([decl(0, 3, 15, 1)]);
            assert.ok(m.has((0 << 16) | 6), 'binding 3 must key as 6');
            assert.ok(!m.has((0 << 16) | 3), 'binding 3 must not key as 3');
        });

        it('should keep the descriptor set in the high half of the key', () => {
            const m = preDceImageInfoFromDecls([decl(2, 1, 15, 1)]);
            assert.ok(m.has((2 << 16) | 2));
        });

        it('should not collide two sets using the same binding', () => {
            const m = preDceImageInfoFromDecls([decl(0, 1, 15, 1), decl(1, 1, 2, 2)]);
            assert.equal(m.size, 2);
            assert.equal(m.get((0 << 16) | 2).format, 'R8Unorm');
            assert.equal(m.get((1 << 16) | 2).format, 'RGBA16Float');
        });
    });

    describe('Image declaration mapping: dimension', () => {
        it('should map SPIR-V Dim 2 to 3D', () => {
            const m = preDceImageInfoFromDecls([decl(0, 0, 2, 2)]);
            assert.equal(m.get(0).dim, '3D');
        });

        it('should map a non-arrayed 2D image to 2D', () => {
            const m = preDceImageInfoFromDecls([decl(0, 0, 15, 1, 0)]);
            assert.equal(m.get(0).dim, '2D');
        });

        it('should map an arrayed 2D image to 2DArray', () => {
            const m = preDceImageInfoFromDecls([decl(0, 0, 15, 1, 1)]);
            assert.equal(m.get(0).dim, '2DArray');
        });

        it('should map a non-arrayed cube image to Cube', () => {
            const m = preDceImageInfoFromDecls([decl(0, 0, 0, 3, 0)]);
            assert.equal(m.get(0).dim, 'Cube');
        });

        it('should map an arrayed cube image to CubeArray', () => {
            const m = preDceImageInfoFromDecls([decl(0, 0, 0, 3, 1)]);
            assert.equal(m.get(0).dim, 'CubeArray');
        });

        it('should map SPIR-V Dim 0 to 1D', () => {
            const m = preDceImageInfoFromDecls([decl(0, 0, 0, 0)]);
            assert.equal(m.get(0).dim, '1D');
        });
    });

    describe('Image declaration mapping: sample type', () => {
        it('should map unsigned integer components to Uint', () => {
            const m = preDceImageInfoFromDecls([decl(0, 0, 0, 1, 0, 0)]);
            assert.equal(m.get(0).sampleType, 'Uint');
        });

        it('should map signed integer components to Sint', () => {
            const m = preDceImageInfoFromDecls([decl(0, 0, 0, 1, 0, 1)]);
            assert.equal(m.get(0).sampleType, 'Sint');
        });

        it('should leave a float component type Undefined, not assert Float', () => {
            // Float is already the correct default at every consumer, and
            // leaving this Undefined is what lets a later post-Tint scan with
            // real evidence win over a declaration-only guess.
            const m = preDceImageInfoFromDecls([decl(0, 0, 15, 1, 0, -1)]);
            assert.equal(m.get(0).sampleType, 'Undefined');
        });
    });

    describe('Image declaration mapping: unusable declarations', () => {
        it('should drop a declaration that resolves to nothing usable', () => {
            // Unknown format, unrecognized dimension, non-integer component:
            // inserting this would shadow a post-Tint scan that does know.
            const m = preDceImageInfoFromDecls([decl(0, 0, 0, 5, 0, -1)]);
            assert.equal(m.size, 0);
        });

        it('should keep a sampled texture that only resolves a dimension', () => {
            // A sampled texture declares format Unknown, so dimension (and
            // sample type, when integer) is all there is to recover -- and it is
            // exactly what a DCE-stripped binding needs.
            const m = preDceImageInfoFromDecls([decl(1, 2, 0, 2)]);
            assert.equal(m.size, 1);
            assert.equal(m.get((1 << 16) | 4).format, 'Undefined');
            assert.equal(m.get((1 << 16) | 4).dim, '3D');
        });
    });
}
