/**************************************************************************/
/*  spirv_spec_constants.h                                                */
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

#pragma once

// Specialization-constant SPIR-V patching + identity hashing, shared between
// the runtime driver's per-draw pipeline creation
// (rendering_device_driver_webgpu.cpp, WEBGPU_ENABLED, web builds) and the
// export-time shader baker (rendering_shader_container_webgpu.cpp,
// WEBGPU_ENABLED || WEBGPU_SHADER_BAKER_ENABLED, native editor builds) -- see
// webgpu_notes/TASKS.md Task 13's 2026-09-20 scoping update for the full
// design this supports (baking recorded specialization-constant pipeline
// variants ahead of time instead of always hitting runtime Tint).
//
// Deliberately has zero dependency on SPIRV-Tools/Tint (unlike
// spirv_preprocess.cpp) so it can be compiled into the native editor's
// baker_sources list (drivers/webgpu/SCsub) without pulling in the vendored
// Tint/SPIRV-Tools sources that WEBGPU_SHADER_BAKER_ENABLED deliberately
// keeps out of the editor (see wgsl_bake_subprocess.h's doc comment).

#include "core/string/ustring.h"
#include "servers/rendering/rendering_device_driver.h"

namespace webgpu {

// Shared file format for export-time-baked specialization-constant variants
// (see webgpu_notes/TASKS.md Task 13's 2026-09-20 scoping update, Phases 2/3):
// a project-local resource written once by the export-time baker
// (rendering_shader_container_webgpu.cpp, via a dedicated EditorExportPlugin)
// and read once by the runtime driver (rendering_device_driver_webgpu.cpp) at
// first use. Deliberately a bespoke, self-contained file rather than an
// extension of RenderingShaderContainer's own binary format: that format's
// footer-read override has no way to tell "this on-disk container predates
// this optional section" from "this container has zero entries in it" (only
// the *current* code's `_format_version()` is available to a subclass, not
// what the file being read actually was written with) -- safely versioning
// that would need changes to the shared, upstream `RenderingShaderContainer`
// base class other RD backends also use. A separate, entirely-optional file
// that simply doesn't exist for a never-baked (or pre-Phase-2) export sidesteps
// that risk completely: its absence is indistinguishable from "no data",
// which is exactly the fallback-to-Tint behavior this feature must default to.
//
// Layout: `[uint32 magic]["count" uint32][count x (uint64 combo_hash, uint32
// wgsl_len, wgsl_len bytes of UTF-8 WGSL source, no NUL terminator)]`.
inline constexpr uint32_t BAKED_SPEC_VARIANTS_MAGIC = 0x53504357; // Arbitrary but stable; changing it is a breaking format change.
inline constexpr const char *BAKED_SPEC_VARIANTS_PATH = "res://.godot/webgpu_baked_spec_variants.bin";

// Patches SPIR-V bytecode to apply specialization constant values (rewrites
// OpSpecConstantTrue/False/OpSpecConstant's literal words on a copy). Pure
// function of its inputs -- calling this with the same SPIR-V and constants
// always produces byte-identical output, which is what lets an export-time
// bake stand in for what the runtime driver would otherwise have computed.
PackedByteArray patch_spirv_spec_constants(const PackedByteArray &p_spirv, VectorView<RDD::PipelineSpecializationConstant> p_constants);

// Same 64-bit MurmurHash3 scheme used throughout drivers/webgpu/ for SPIR-V
// identity (two passes with different seeds, combined into 64 bits to avoid
// the birthday-paradox collisions a 32-bit hash would risk once a project
// has on the order of ~1k distinct shaders).
uint64_t hash_spirv(const uint8_t *p_spirv_ptr, int p_spirv_size);

// Combines a base (pre-patch) SPIR-V's hash with one specific set of
// specialization-constant values into a single 64-bit identity. Used both to
// key baked variants at export time and to look them up at runtime, so the
// two sides must agree byte-for-byte on how this is computed -- this is the
// single shared implementation both call, rather than two independent copies
// that could silently drift apart.
uint64_t hash_spec_constant_combo(uint64_t p_base_spirv_hash, VectorView<RDD::PipelineSpecializationConstant> p_constants);

// --- Export-time baking accumulator (native editor, WEBGPU_SHADER_BAKER_ENABLED
// only) ---
//
// A recorded-usage table (produced by a Phase 1 browser recording session,
// see rendering_device_driver_webgpu.cpp's _record_spec_constant_usage()) is
// parsed once per export by WebGPUSpecConstantBakerExportPlugin
// (editor/shader/shader_baker/) and installed here before shader baking
// starts. RenderingShaderContainerWebGPU::_set_code_from_spirv() -- which
// runs once per shader stage, potentially concurrently across
// WorkerThreadPool baking threads (see ShaderBakerExportPlugin) -- reads it
// read-only to find recorded constant-sets for the stage it's currently
// baking, bakes any it finds, and records the results here too. All
// functions below are mutex-protected for that concurrent-baking reason.
struct SpecConstantUsageEntry {
	uint64_t base_spv_hash = 0;
	Vector<RDD::PipelineSpecializationConstant> constants;
};

void set_spec_constant_usage_table(const Vector<SpecConstantUsageEntry> &p_entries);
void clear_spec_constant_usage_data();

// Every recorded constant-set for one base SPIR-V hash -- empty if none, or
// if no table was ever installed (the common case: a project that hasn't
// captured a Phase 1 recording, where this whole feature is a no-op). Each
// call that finds a non-empty result marks that hash "matched" for
// get_spec_constant_usage_match_stats() below -- this is the only place that
// ever queries the table, so it's a complete, exact record of which recorded
// hashes turned out to correspond to a real shader in this export.
Vector<Vector<RDD::PipelineSpecializationConstant>> get_spec_constant_usage_for_hash(uint64_t p_base_spv_hash);

// Diagnostic for the "stale recording" gotcha (see webgpu_notes/TASKS.md Task
// 13's 2026-09-20 write-up): a recording's base_spv_hash values are only
// valid against the exact build's SPIR-V they were captured from -- rebuilding
// the editor (even for unrelated reasons) has been observed to regenerate
// `.godot/shader_cache` non-deterministically, silently invalidating some or
// all of them. `matched_hashes` counts distinct recorded hashes that were
// actually found in this export's shader set at all; `total_hashes` is how
// many distinct hashes the installed usage table had. A low ratio here (not
// checked automatically -- see WebGPUSpecConstantBakerExportPlugin for where
// this is read and turned into a WARN_PRINT) means most of the recording is
// stale and should be re-captured against the current build before it can
// meaningfully help. (0, 0) if no table was ever installed.
struct SpecConstantMatchStats {
	uint32_t matched_hashes = 0;
	uint32_t total_hashes = 0;
};
SpecConstantMatchStats get_spec_constant_usage_match_stats();

struct BakedSpecConstantVariant {
	uint64_t combo_hash = 0;
	String wgsl;
};

void record_baked_spec_constant_variant(uint64_t p_combo_hash, const String &p_wgsl);

// Everything recorded via record_baked_spec_constant_variant() so far,
// removed from the accumulator. Called once, by
// WebGPUSpecConstantBakerExportPlugin::_end_customize_resources(), after
// every shader has finished baking (see that class's doc comment for why
// _export_end() -- the more obviously-named hook -- is the wrong one: it
// fires after the package has already been written).
Vector<BakedSpecConstantVariant> take_baked_spec_constant_variants();

} // namespace webgpu
