// Copyright 2020 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/tint/lang/spirv/reader/reader.h"

#include <span>

#include "src/tint/lang/core/ir/module.h"
#include "src/tint/lang/core/ir/validator.h"
#include "src/tint/lang/spirv/reader/lower/lower.h"
#include "src/tint/lang/spirv/reader/parser/parser.h"

namespace tint::spirv::reader {

Result<core::ir::Module> ReadIR(const std::vector<uint32_t>& input, const Options& options) {
    // Parse the input SPIR-V to the SPIR-V dialect of the IR.
    TINT_CHECK_RESULT_UNWRAP(mod, Parse(input, options));
    mod.dump_ir_when_validating = options.dump_ir_when_validating;
    mod.enable_validation_asserts = options.enable_validation_asserts;

    // Lower the module to the core dialect of the IR.
    TINT_CHECK_RESULT(Lower(mod));

    // Always validate the core IR, so that we fail somewhat gracefully on invalid inputs instead of
    // just ICEing later on.
    //
    // GODOT WEBGPU PATCH: added kAllowPhonyInstructions, as defense-in-depth
    // alongside the kTargetEnv bump in parser/parser.cc -- see that patch's
    // comment and webgpu_notes/TASKS.md Task 8.6 for why Godot's real WebGPU
    // pipeline doesn't currently reach SPIR-V 1.4 at all. If it ever does:
    // parser.cc's AddRefToOutputsIfNeeded() inserts a `phony = val;` for every
    // resource variable listed in a SPIR-V 1.4+ OpEntryPoint interface (which,
    // unlike older SPIR-V, includes all UniformConstant/StorageBuffer/etc.
    // globals the entry point touches, not just Input/Output) so the reader
    // can't silently lose track of a declared-but-unread binding -- and this
    // validation call would reject those with "missing capability
    // 'kAllowPhonyInstructions'". Two of this reader's own lowering passes
    // (lower/shader_io.cc, lower/vector_element_pointer.cc) already pass this
    // same capability to their own internal self-validation calls, so this is
    // just extending that same allowance to the reader's final validation.
    // See thirdparty/README.md's `## tint` patch list.
    TINT_CHECK_RESULT(core::ir::Validate(mod,
                                         core::ir::Capabilities{
                                             core::ir::Capability::kAllowMultipleEntryPoints,
                                             core::ir::Capability::kAllowOverrides,
                                             core::ir::Capability::kAllowStructMemberSizeMismatch,
                                             core::ir::Capability::kAllowPhonyInstructions,
                                         },
                                         "after spirv::ReadIR"));

    return mod;
}

}  // namespace tint::spirv::reader
