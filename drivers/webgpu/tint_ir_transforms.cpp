/**************************************************************************/
/*  tint_ir_transforms.cpp                                                */
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

#include "tint_ir_transforms.h"

#include "src/tint/lang/core/ir/function.h"
#include "src/tint/lang/core/ir/module.h"
#include "src/tint/lang/core/ir/value.h"
#include "src/tint/lang/core/ir/var.h"
#include "src/tint/lang/core/type/manager.h"
#include "src/tint/lang/core/type/pointer.h"

#include <functional>

namespace webgpu_tint {

void DemoteVertexStageReadWriteStorage(tint::core::ir::Module &p_module) {
	using namespace tint::core;

	bool is_vertex_stage = false;
	for (auto *func : p_module.functions) {
		if (func->IsVertex()) {
			is_vertex_stage = true;
			break;
		}
	}
	if (!is_vertex_stage) {
		return;
	}

	type::Manager &ty = p_module.Types();

	// Narrows a storage-address-space pointer value's access mode from
	// read_write to read, then recurses into every use of that value that
	// itself produces a pointer -- e.g. an access-chain instruction like
	// `vbuf.data[i]` gets its own explicit pointer-typed result, computed once
	// (from the base var's access mode) when Tint's SPIR-V reader built the IR.
	// Patching the var's type alone leaves those derived results stale
	// (still read_write), which Tint's IR validator rejects as inconsistent
	// with the var they were derived from.
	std::function<void(ir::Value *)> demote = [&](ir::Value *p_value) {
		auto *ptr_type = p_value->Type()->As<type::Pointer>();
		if (!ptr_type || ptr_type->AddressSpace() != AddressSpace::kStorage || ptr_type->Access() != Access::kReadWrite) {
			return;
		}
		p_value->SetType(ty.ptr(AddressSpace::kStorage, ptr_type->StoreType(), Access::kRead));
		p_value->ForEachUseUnsorted([&](ir::Usage p_use) {
			if (auto *result = p_use.instruction->Result(0)) {
				demote(result);
			}
		});
	};

	for (auto *inst : *p_module.root_block) {
		if (auto *var = inst->As<ir::Var>()) {
			demote(var->Result());
		}
	}
}

} // namespace webgpu_tint
