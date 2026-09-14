/**************************************************************************/
/*  tint_ir_transforms.h                                                  */
/**************************************************************************/
/*                       This file is part of:                            */
/*                           GODOT ENGINE                                 */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* WebGPU-specific structural (Tint core::ir::Module) transforms, applied  */
/* between Tint's SPIR-V reader and WGSL writer -- see tint_wrapper.cpp.   */
/* Unlike tint_wrapper.h, this header is NOT part of the C++17/C++20       */
/* boundary: it's only ever included from tint_wrapper.cpp, which is      */
/* itself compiled in the Tint C++20 environment, so it can reference     */
/* Tint types directly.                                                   */
/**************************************************************************/

#ifndef TINT_IR_TRANSFORMS_H
#define TINT_IR_TRANSFORMS_H

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

#endif // TINT_IR_TRANSFORMS_H
