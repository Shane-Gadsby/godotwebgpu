/**************************************************************************/
/*  tint_ir_transforms.h                                                  */
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

namespace tint::core::ir {
class Module;
}

namespace webgpu_tint {

// WebGPU forbids storage buffers bound with read_write/write access from being
// visible to the vertex stage (undefined per-invocation write ordering/duplication
// hazards) -- fragment and compute stages have no such restriction. Demotes any
// module-scope `read_write` storage-address-space variable to `read` when the
// module's entry point is a vertex stage; a no-op for any other stage.
//
// Godot's SPIR-V is generated per shader stage (glslang emits one entry point per
// module), so it's sufficient to check whether *the* module's entry point is a
// vertex stage rather than tracing which module-scope vars that entry point
// specifically reaches -- equivalent to (and replacing) the old text-based
// demotion this driver used to apply to the entire WGSL string for any shader
// stage bytecode classified as SHADER_STAGE_VERTEX. See Task 9.1/9 and Task 7.18
// (webgpu_notes/TASKS.md) for why the demotion is vertex-only, and why doing it
// structurally (via the IR type system) instead of via string scanning removes
// the fixed-length in-place memcpy fragility Task 7.18 flagged.
void DemoteVertexStageReadWriteStorage(tint::core::ir::Module &p_module);

} // namespace webgpu_tint
