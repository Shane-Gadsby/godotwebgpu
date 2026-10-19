/**
 * Tests for drivers/webgpu/spirv_binding_layout.h: where the elements of an
 * expanded texture array land in a bind group, and the scene shaders'
 * SamplerLodBias binding.
 *
 * expand_binding_arrays() (spirv_opt_passes.cpp) gives element k > 0 of the
 * array at GLSL binding b its own WGSL binding, and the driver builds bind
 * group layouts and bind groups from the same mapping. Nothing at runtime
 * would notice a collision between those bindings and an ordinary doubled
 * one -- WebGPU would just reject the layout, or bind the wrong texture -- so
 * the layout's invariants are checked here, against the header itself rather
 * than a copy of its numbers.
 *
 * It also cross-checks the shaders and C++ that have to agree with it: every
 * engine texture array the expansion can handle, and binding 40 being the
 * SamplerLodBias block (and nothing else) in both scene renderers and in
 * MaterialStorage::Samplers::get_lod_bias_uniform().
 */

import { readFileSync, readdirSync, statSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { describe, it, assert } from './test_harness.mjs';

const REPO = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const read = (p) => readFileSync(join(REPO, p), 'utf8');

const HEADER = read('drivers/webgpu/spirv_binding_layout.h');
const constant = (name) => {
    const m = HEADER.match(new RegExp(`constexpr uint32_t ${name} = (\\d+);`));
    if (!m) {
        throw new Error(`${name} not found in spirv_binding_layout.h`);
    }
    return Number(m[1]);
};

const BASE = constant('ARRAY_ELEMENT_BINDING_BASE');
const STRIDE = constant('ARRAY_ELEMENT_BINDING_STRIDE');
const MAX_BINDINGS = constant('MAX_BINDINGS_PER_BIND_GROUP');
const LOD_BIAS_BINDING = constant('SAMPLER_LOD_BIAS_GLSL_BINDING');
const MAX_LENGTH = STRIDE + 1;
const PC_RING_BINDING = 120; // PUSH_CONSTANT_RING_BINDING, set 3

// Mirrors of the header's two constexpr functions.
const elementBinding = (b, k) => (k === 0 ? b * 2 : BASE + b * STRIDE + (k - 1));
const canExpand = (b, n) => n >= 2 && n <= MAX_LENGTH && elementBinding(b, n - 1) < MAX_BINDINGS;

function glslFiles(dir) {
    const out = [];
    for (const name of readdirSync(join(REPO, dir))) {
        const p = join(dir, name);
        if (statSync(join(REPO, p)).isDirectory()) {
            out.push(...glslFiles(p));
        } else if (name.endsWith('.glsl')) {
            out.push(p);
        }
    }
    return out;
}

export function runTests() {
    describe('Expanded texture array bindings (spirv_binding_layout.h)', () => {
        it('the mapping in the header is the one mirrored here', () => {
            assert.ok(HEADER.includes('return p_element == 0 ? p_glsl_binding * 2 : ARRAY_ELEMENT_BINDING_BASE + p_glsl_binding * ARRAY_ELEMENT_BINDING_STRIDE + (p_element - 1);'),
                'array_element_binding() changed; update this test with it');
        });

        it('element 0 keeps the ordinary doubled binding', () => {
            for (let b = 0; b < 40; b++) {
                assert.equal(elementBinding(b, 0), b * 2);
            }
        });

        it('no expanded element collides with an ordinary binding or the push-constant ring', () => {
            // Ordinary bindings are 2b and 2b + 1 for GLSL bindings below 200.
            for (let b = 0; b < 64; b++) {
                for (let k = 1; k < MAX_LENGTH; k++) {
                    if (!canExpand(b, k + 1)) {
                        continue;
                    }
                    const w = elementBinding(b, k);
                    assert.ok(w >= 400, `array ${b} element ${k} at ${w} overlaps the doubled range`);
                    assert.ok(w !== PC_RING_BINDING, `array ${b} element ${k} lands on the push-constant ring`);
                }
            }
        });

        it('every (binding, element) pair maps to its own WGSL binding below the WebGPU limit', () => {
            const seen = new Map();
            for (let b = 0; b < 64; b++) {
                for (let n = 2; n <= MAX_LENGTH; n++) {
                    if (!canExpand(b, n)) {
                        continue;
                    }
                    for (let k = 1; k < n; k++) {
                        const w = elementBinding(b, k);
                        assert.ok(w < MAX_BINDINGS, `array ${b} element ${k} at ${w} is past maxBindingsPerBindGroup`);
                        const key = `${b}:${k}`;
                        assert.ok(!seen.has(w) || seen.get(w) === key, `${key} and ${seen.get(w)} share binding ${w}`);
                        seen.set(w, key);
                    }
                }
            }
        });

        it('arrays too long or too high stay collapsed instead of overflowing', () => {
            assert.ok(!canExpand(0, MAX_LENGTH + 1), `an array of ${MAX_LENGTH + 1} must not expand`);
            assert.ok(!canExpand(0, 1), 'a one-element array has nothing to expand');
            const firstUnfit = [...Array(100).keys()].find((b) => !canExpand(b, MAX_LENGTH));
            assert.ok(elementBinding(firstUnfit - 1, MAX_LENGTH - 1) < MAX_BINDINGS);
        });

        it("every engine texture array of 17 or fewer elements fits the layout", () => {
            // The arrays the renderer actually declares (VoxelGI, lightmaps, particle
            // colliders, SDFGI occlusion). Each must be expandable on binding grounds;
            // whether one expands in a given shader is the per-stage budget's call.
            const decl = /layout\(([^)]*)\)\s*uniform\s+(?:restrict\s+|readonly\s+|writeonly\s+)*(?:highp\s+)?\w*(?:texture|image|sampler)\w*\s+(\w+)\s*\[\s*([A-Z_0-9* ]+)\s*\]/g;
            const macros = { MAX_VOXEL_GI_INSTANCES: 8, MAX_LIGHTMAP_TEXTURES: 8, MAX_3D_TEXTURES: 7 };
            let found = 0;
            for (const file of glslFiles('servers/rendering/renderer_rd/shaders')) {
                const src = readFileSync(join(REPO, file), 'utf8');
                for (const m of src.matchAll(decl)) {
                    const binding = /binding\s*=\s*(\d+)/.exec(m[1]);
                    const length = m[3].trim().split('*').map((t) => macros[t.trim()] ?? Number(t.trim())).reduce((a, x) => a * x, 1);
                    if (!binding || !(length >= 2) || length > MAX_LENGTH) {
                        continue;
                    }
                    found++;
                    assert.ok(canExpand(Number(binding[1]), length), `${file}: ${m[2]}[${length}] at binding ${binding[1]} cannot expand`);
                }
            }
            assert.ok(found >= 5, `expected the engine's texture arrays, found ${found}`);
        });
    });

    describe('SamplerLodBias binding', () => {
        for (const file of ['servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered_inc.glsl',
            'servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile_inc.glsl']) {
            it(`${file.split('/').pop()} declares SamplerLodBias at binding ${LOD_BIAS_BINDING}, and nothing else there`, () => {
                const src = read(file);
                const uses = [...src.matchAll(new RegExp(`set = 1, binding = ${LOD_BIAS_BINDING}\\b`, 'g'))];
                assert.equal(uses.length, 1, `set 1 binding ${LOD_BIAS_BINDING} is declared ${uses.length} times`);
                assert.ok(new RegExp(`set = 1, binding = ${LOD_BIAS_BINDING}, std140\\) uniform SamplerLodBias`).test(src));
            });
        }
        it('MaterialStorage binds it at the same number', () => {
            const src = read('servers/rendering/renderer_rd/storage_rd/material_storage.cpp');
            const fn = src.slice(src.indexOf('RD::Uniform MaterialStorage::Samplers::get_lod_bias_uniform() const {'));
            assert.ok(new RegExp(`u\\.binding = ${LOD_BIAS_BINDING};`).test(fn.slice(0, 400)), 'get_lod_bias_uniform() binds a different number');
        });
    });
}
