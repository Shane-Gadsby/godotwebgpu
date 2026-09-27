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

// Patches SPIR-V bytecode to apply specialization constant values (rewrites
// OpSpecConstantTrue/False/OpSpecConstant's literal words on a copy). Pure
// function of its inputs -- calling this with the same SPIR-V and constants
// always produces byte-identical output, which is what lets an export-time
// bake stand in for what the runtime driver would otherwise have computed.
PackedByteArray patch_spirv_spec_constants(const PackedByteArray &p_spirv, VectorView<RDD::PipelineSpecializationConstant> p_constants);

} // namespace webgpu
