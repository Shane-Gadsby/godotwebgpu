/**************************************************************************/
/*  spirv_lite_reflect.h                                                 */
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

// Reflection over raw SPIR-V that is a plain walk of the instruction stream:
// no SPIRV-Tools, no Tint, no preprocessing passes.
//
// Separate from spirv_preprocess.h for one reason: *who can compile it*. The
// export-time shader baker is a native editor build, and drivers/webgpu/SCsub
// deliberately keeps the vendored Tint/SPIRV-Tools sources out of it (see
// wgsl_bake_subprocess.h for why -- Tint can abort() the whole editor on a
// shader variant it does not handle, so the editor shells out to
// bin/tint_convert_cli instead of linking Tint). spirv_preprocess.cpp needs
// SPIRV-Tools and so cannot go in that build, but these two functions need
// nothing at all -- and the baker needs both, to record in each shader
// container what the runtime would otherwise have to re-derive from the SPIR-V
// at load time.
//
// Compiled into all three places that need it: the web driver (via SCsub's
// glob), the native editor's baker subset (via SCsub's baker_sources), and
// tint_convert_cli (via tint_cli/build.sh).

#include "core/templates/vector.h"

#include <cstdint>

namespace spirv_lite_reflect {

// One UniformConstant image variable, exactly as the *raw* SPIR-V declares it:
// before any preprocessing pass has run, and before dead-resource elimination
// has had a chance to remove the declaration.
//
// Deliberately raw SPIR-V operand values rather than WebGPU enums, because the
// two callers do not both speak WebGPU: the runtime driver maps these to
// WGPUTextureFormat / WGPUTextureViewDimension / WGPUTextureSampleType for a
// BindGroupLayout entry, while the baker only records them into the container
// and has no WebGPU headers in its build at all. Keeping the shared half
// enum-free is what lets the walk exist exactly once.
//
// `binding` is the GLSL binding number -- that is, before
// spirv_preprocess::split_combined_samplers() doubles it. A consumer that keys
// on final WGSL bindings has to double it to land in the same key space.
struct RawImageDecl {
	uint32_t set = 0;
	uint32_t binding = 0;
	uint32_t spv_format = 0; // SPIR-V ImageFormat operand (0 = Unknown).
	uint32_t spv_dim = 0; // SPIR-V Dim operand: 0=1D, 1=2D, 2=3D, 3=Cube, ...
	uint32_t arrayed = 0;
	// Signedness of the image's component type: 1 = signed int, 0 = unsigned
	// int, -1 = not an integer type at all -- the overwhelmingly common case,
	// and the one where a float sample type is already the right default.
	int32_t int_signedness = -1;
};

// Every UniformConstant image variable the module declares, resolved to its
// (set, binding) and the type-level facts about it.
//
// This exists because a declared image's format, dimension and component type
// are type-level information that survives only as long as the declaration
// does. spirv_preprocess::eliminate_dead_resources() correctly strips a binding
// a given stage's entry point cannot reach, and once it is gone there is
// nothing left in that stage's WGSL for the driver's post-Tint text scans to
// read those facts out of. They come up empty, and the BindGroupLayout entry
// silently defaults to the wrong format, dimension or sample type -- which Dawn
// then rejects at bind time with an error naming a texture the shader does not
// appear to use at all. Reading the raw bytes first is what makes those
// bindings recoverable. See webgpu_notes/TASKS.md Task 9.5 rounds 18-27.
//
// Safe to run on the original bytes: none of the preprocessing passes affect a
// plain OpTypeImage / OpVariable / OpDecorate triad.
Vector<RawImageDecl> extract_raw_image_decls(const Vector<uint8_t> &p_bytes);

// True when the module declares at least one SpecId-decorated specialization
// constant. spirv_preprocess::has_spec_constants() is this, and delegates here
// so the predicate has one implementation rather than one per build.
bool has_spec_id_decoration(const Vector<uint8_t> &p_bytes);

} // namespace spirv_lite_reflect
