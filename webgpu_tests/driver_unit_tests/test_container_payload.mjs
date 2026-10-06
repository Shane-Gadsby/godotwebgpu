/**
 * Tests for RenderingShaderContainerWebGPU's two payloads: which of them a
 * container has to ship, and how the footer that carries the baked half is
 * laid out.
 *
 * Mirrors drivers/webgpu/rendering_shader_container_webgpu.cpp.
 *
 * Why this is worth pinning down without a build:
 *
 *  - The omission rule decides whether a container ships its SPIR-V. Getting it
 *    wrong in the permissive direction does not fail loudly: the shader loads,
 *    renders, and quietly ignores its specialization constants, because the
 *    legacy specialization path re-patches SPIR-V that is no longer there. (The
 *    driver does print an error if it ever sees that combination -- but the
 *    point of the rule is that it cannot.)
 *
 *  - The footer is written by one function and read by another, and the base
 *    container asserts that the two agree to the byte ("Amount of bytes in the
 *    container does not match the amount of bytes read"). Any drift between
 *    them makes every shader in an export fail to parse.
 */

import { describe, it, assert } from './test_harness.mjs';

// --- Simulate _wgsl_declares_override() ---
// Structurally the same test the driver makes on the same text: "@id(",
// digits, ")". The driver is the authority, since what it decides is what
// actually happens at pipeline creation.
function wgslDeclaresOverride(wgsl) {
    return /@id\(\d+\)/.test(wgsl);
}

// --- Simulate _set_code_from_spirv()'s omission decision ---
// Each stage is { wgsl, hasSpecConstants }; wgsl is null/'' when the bake
// failed for that stage. Returns true when the container must keep its SPIR-V.
function spirvNeeded(stages) {
    for (const stage of stages) {
        if (!stage.wgsl) {
            return true; // No WGSL: the runtime has to run Tint on the bytes.
        }
        if (stage.hasSpecConstants && !wgslDeclaresOverride(stage.wgsl)) {
            return true; // Legacy specialization path re-patches the SPIR-V.
        }
    }
    return false;
}

const IMAGE_DECL_WORDS = 6;

// --- Simulate _to_bytes_footer_extra_data() + _baked_metadata_to_bytes() ---
// Byte count only: what matters is that the reader walks exactly this far.
function footerSize(stages, imageDeclsBaked) {
    let size = 0;
    for (const stage of stages) {
        size += 4; // uint32 WGSL length
        size += stage.wgsl ? Buffer.byteLength(stage.wgsl, 'utf-8') : 0;
    }
    if (imageDeclsBaked) {
        for (const stage of stages) {
            size += 4; // uint32 stage flags
            size += 4; // uint32 declaration count
            size += (stage.imageDecls?.length ?? 0) * IMAGE_DECL_WORDS * 4;
        }
    }
    return size;
}

// --- Simulate _from_bytes_footer_extra_data() ---
// Walks the same layout and reports how far it got, plus what it recovered.
//
// `available` is how many footer bytes actually exist, which the C++ checks
// before every read: a length or a declaration count comes off disk, and a
// count is multiplied by a record size. On a short read it stops, which makes
// the base container's "did we consume exactly the whole thing" check fail --
// the right outcome for a corrupt container, where reading past the end is not.
function parseFooter(stages, imageDeclsBaked, available = Infinity) {
    let offset = 0;
    const hasRoom = (at, need) => at + need <= available;
    const views = [];
    for (const stage of stages) {
        if (!hasRoom(offset, 4)) { break; }
        offset += 4;
        const len = stage.wgsl ? Buffer.byteLength(stage.wgsl, 'utf-8') : 0;
        if (!hasRoom(offset, len)) { break; }
        views.push(len > 0 ? { offset, length: len } : { offset: 0, length: 0 });
        offset += len;
    }
    const specFlags = [];
    const decls = [];
    if (imageDeclsBaked) {
        for (const stage of stages) {
            if (!hasRoom(offset, 8)) { break; }
            offset += 4;
            specFlags.push(!!stage.hasSpecConstants);
            const count = stage.imageDecls?.length ?? 0;
            offset += 4;
            if (!hasRoom(offset, count * IMAGE_DECL_WORDS * 4)) { break; }
            decls.push(count);
            offset += count * IMAGE_DECL_WORDS * 4;
        }
    }
    return { consumed: offset, views, specFlags, decls };
}

function stage(wgsl, hasSpecConstants = false, imageDecls = []) {
    return { wgsl, hasSpecConstants, imageDecls };
}

export function runTests() {
    describe('Container payload: SPIR-V omission rule', () => {
        it('should drop the SPIR-V when every stage baked and none has spec constants', () => {
            assert.equal(spirvNeeded([stage('fn main() {}'), stage('fn main() {}')]), false);
        });

        it('should drop the SPIR-V when spec constants survived as overrides', () => {
            assert.equal(spirvNeeded([stage('@id(0) override lod: u32 = 0u;', true)]), false);
        });

        it('should keep the SPIR-V when a stage failed to bake', () => {
            // That stage still needs the runtime Tint fallback.
            assert.equal(spirvNeeded([stage('fn main() {}'), stage(null)]), true);
        });

        it('should keep the SPIR-V when a stage was frozen to its defaults', () => {
            // Declares spec constants but Tint emitted no override to set them
            // through, so pipeline creation takes the legacy re-patch path.
            assert.equal(spirvNeeded([stage('fn main() {}', true)]), true);
        });

        it('should keep the SPIR-V when any one stage of several is frozen', () => {
            assert.equal(spirvNeeded([
                stage('@id(0) override a: u32 = 0u;', true),
                stage('fn main() {}', true),
            ]), true);
        });

        it('should not be fooled by @id in a comment-like position without digits', () => {
            // The test is structural; "@id(" alone is not an override.
            assert.equal(spirvNeeded([stage('// @id(x) not an override', true)]), true);
        });

        it('should ignore spec constants on a stage that has overrides', () => {
            assert.equal(spirvNeeded([stage('@id(12) override quality: i32 = 2;', true)]), false);
        });
    });

    describe('Container payload: footer round-trip', () => {
        const cases = [
            { name: 'no stages', stages: [] },
            { name: 'one baked stage, no declarations', stages: [stage('fn main() {}')] },
            {
                name: 'two baked stages with declarations',
                stages: [
                    stage('fn vs() {}', false, [{}, {}]),
                    stage('fn fs() {}', true, [{}]),
                ],
            },
            {
                name: 'an unbaked stage among baked ones',
                stages: [stage('fn vs() {}', false, [{}]), stage(null, false, []), stage('fn fs() {}')],
            },
            {
                name: 'multi-byte UTF-8 in the WGSL',
                // Lengths are byte counts, not character counts: a length
                // prefix measured in characters desynchronises the whole footer.
                stages: [stage('// ° § 日本\nfn main() {}')],
            },
        ];

        for (const c of cases) {
            it(`should read back exactly what it wrote: ${c.name} (declarations baked)`, () => {
                const size = footerSize(c.stages, true);
                assert.equal(parseFooter(c.stages, true).consumed, size);
            });

            it(`should read back exactly what it wrote: ${c.name} (no baked block)`, () => {
                // A container written before the baked block existed has
                // flags == 0, so neither side writes or reads it.
                const size = footerSize(c.stages, false);
                assert.equal(parseFooter(c.stages, false).consumed, size);
            });
        }

        it('should not read the baked block when the flag is clear', () => {
            // The asymmetric case: the reader trusting the flag is what keeps a
            // pre-flag container parsing correctly instead of running off the end.
            const stages = [stage('fn main() {}', true, [{}, {}])];
            assert.equal(parseFooter(stages, false).consumed, footerSize(stages, false));
            assert.ok(footerSize(stages, true) > footerSize(stages, false));
        });

        it('should give each baked stage a window at the right offset', () => {
            const stages = [stage('abc'), stage('de')];
            const { views } = parseFooter(stages, true);
            assert.equal(views[0].offset, 4); // after the first length prefix
            assert.equal(views[0].length, 3);
            assert.equal(views[1].offset, 11); // 4 + 3 + 4
            assert.equal(views[1].length, 2);
        });

        it('should mark an unbaked stage with a zero-length window', () => {
            const { views } = parseFooter([stage(null), stage('x')], true);
            assert.equal(views[0].length, 0);
            assert.equal(views[1].length, 1);
        });

        it('should stop short when the footer is truncated mid-WGSL', () => {
            // Stopping short is what makes the container fail to parse. Reading
            // the declared length anyway would run off the end of the buffer.
            const stages = [stage('abcdefgh')];
            const full = footerSize(stages, true);
            const got = parseFooter(stages, true, full - 3).consumed;
            assert.ok(got < full, `expected a short read, got ${got} of ${full}`);
        });

        it('should stop short when a declaration count runs past the end', () => {
            // The count is a 32-bit number from the file times a record size,
            // so this is the read most worth guarding.
            const stages = [stage('x', false, [{}, {}, {}, {}])];
            const full = footerSize(stages, true);
            const got = parseFooter(stages, true, full - 24).consumed;
            assert.ok(got < full, `expected a short read, got ${got} of ${full}`);
        });

        it('should consume exactly the footer when it is complete', () => {
            const stages = [stage('abc', true, [{}, {}]), stage('de')];
            const full = footerSize(stages, true);
            assert.equal(parseFooter(stages, true, full).consumed, full);
        });

        it('should recover each stage spec-constant flag and declaration count', () => {
            const stages = [stage('a', true, [{}, {}, {}]), stage('b', false, [])];
            const { specFlags, decls } = parseFooter(stages, true);
            assert.equal(specFlags[0], true);
            assert.equal(specFlags[1], false);
            assert.equal(decls[0], 3);
            assert.equal(decls[1], 0);
        });
    });
}
