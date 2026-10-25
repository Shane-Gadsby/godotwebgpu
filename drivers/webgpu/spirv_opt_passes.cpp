/**************************************************************************/
/*  spirv_opt_passes.cpp                                                  */
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

#include "spirv_opt_passes.h"

#include "source/opcode.h"
#include "source/opt/build_module.h"
#include "source/opt/ir_builder.h"
#include "source/opt/ir_context.h"
#include "source/util/string_utils.h"
#include "spirv-tools/libspirv.hpp"
#include "spirv_binding_layout.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace spirv_opt_passes {

using namespace spvtools;
namespace analysis = opt::analysis;

namespace {

// GLSL.std.450 extended instruction numbers.
constexpr uint32_t GLSL_STD_450_FABS = 4;
constexpr uint32_t GLSL_STD_450_LOG2 = 30;
constexpr uint32_t GLSL_STD_450_FMAX = 40;
constexpr uint32_t GLSL_STD_450_FCLAMP = 43;

const opt::IRContext::Analysis PRESERVED = opt::IRContext::kAnalysisDefUse | opt::IRContext::kAnalysisInstrToBlockMapping;

// Instruction-stream scan, so modules a pass has nothing to do in never pay for
// building the IR.
template <typename F>
void for_each_raw_instruction(const std::vector<uint32_t> &p_words, F p_fn) {
	size_t pos = 5;
	while (pos < p_words.size()) {
		const uint32_t wc = p_words[pos] >> 16;
		if (wc == 0 || pos + wc > p_words.size()) {
			return;
		}
		if (!p_fn((spv::Op)(p_words[pos] & 0xFFFF), &p_words[pos], wc)) {
			return;
		}
		pos += wc;
	}
}

std::unique_ptr<opt::IRContext> build(const std::vector<uint32_t> &p_words) {
	if (p_words.size() < 5 || !check_ir_preconditions(p_words.data(), p_words.size())) {
		return nullptr;
	}
	return BuildModule(SPV_ENV_VULKAN_1_2, [](spv_message_level_t, const char *, const spv_position_t &, const char *) {}, p_words.data(), p_words.size());
}

void emit(opt::IRContext *p_ctx, std::vector<uint32_t> &r_words) {
	std::vector<uint32_t> out;
	p_ctx->module()->ToBinary(&out, /* skip_nop = */ true);
	r_words.swap(out);
}

// OpEntryPoint in-operands: execution model, function, name, then interface ids.
constexpr uint32_t ENTRY_POINT_FIRST_INTERFACE = 3;

// Replaces p_id in every entry point's interface with p_replacements (which
// may be empty to just drop it). SPIR-V 1.4+ lists every global an entry
// point uses there, handles included.
void replace_in_interfaces(opt::IRContext *p_ctx, uint32_t p_id, const std::vector<uint32_t> &p_replacements) {
	for (opt::Instruction &ep : p_ctx->module()->entry_points()) {
		bool found = false;
		std::vector<opt::Operand> operands;
		for (uint32_t i = 0; i < ep.NumInOperands(); i++) {
			if (i >= ENTRY_POINT_FIRST_INTERFACE && ep.GetSingleWordInOperand(i) == p_id) {
				found = true;
				continue;
			}
			operands.push_back(ep.GetInOperand(i));
		}
		if (!found) {
			continue;
		}
		for (uint32_t id : p_replacements) {
			operands.push_back(opt::Operand(SPV_OPERAND_TYPE_ID, { id }));
		}
		ep.SetInOperands(std::move(operands));
		p_ctx->UpdateDefUse(&ep);
	}
}

std::vector<opt::Instruction *> users_of(opt::IRContext *p_ctx, uint32_t p_id) {
	std::vector<opt::Instruction *> users;
	p_ctx->get_def_use_mgr()->ForEachUser(p_id, [&users](opt::Instruction *p_user) { users.push_back(p_user); });
	return users;
}

bool is_debug_or_annotation(spv::Op p_op) {
	return p_op == spv::Op::OpName || p_op == spv::Op::OpMemberName || p_op == spv::Op::OpDecorate || p_op == spv::Op::OpDecorateId ||
			p_op == spv::Op::OpMemberDecorate || p_op == spv::Op::OpGroupDecorate || p_op == spv::Op::OpEntryPoint;
}

// A constant of p_type_id with every component p_scalar_id (p_scalar_id itself for a scalar type).
uint32_t splat(opt::IRContext *p_ctx, uint32_t p_type_id, uint32_t p_scalar_id) {
	const analysis::Type *type = p_ctx->get_type_mgr()->GetType(p_type_id);
	const analysis::Vector *vec = type->AsVector();
	if (!vec) {
		return p_scalar_id;
	}
	analysis::ConstantManager *cm = p_ctx->get_constant_mgr();
	const analysis::Constant *c = cm->GetConstant(type, std::vector<uint32_t>(vec->element_count(), p_scalar_id));
	return cm->GetDefiningInstruction(c, p_type_id)->result_id();
}

uint32_t glsl_std_450(opt::IRContext *p_ctx) {
	uint32_t id = p_ctx->get_feature_mgr()->GetExtInstImportId_GLSLstd450();
	if (id == 0) {
		p_ctx->AddExtInstImport("GLSL.std.450");
		id = p_ctx->get_feature_mgr()->GetExtInstImportId_GLSLstd450();
	}
	return id;
}

std::vector<opt::Instruction *> instructions_with_opcode(opt::IRContext *p_ctx, std::initializer_list<spv::Op> p_ops) {
	std::vector<opt::Instruction *> found;
	for (opt::Function &function : *p_ctx->module()) {
		for (opt::BasicBlock &block : function) {
			for (opt::Instruction &inst : block) {
				for (spv::Op op : p_ops) {
					if (inst.opcode() == op) {
						found.push_back(&inst);
						break;
					}
				}
			}
		}
	}
	return found;
}

// ---- lower_wgsl_unsupported_ops ----

bool strip_early_fragment_tests(opt::IRContext *p_ctx) {
	std::vector<opt::Instruction *> modes;
	for (opt::Instruction &mode : p_ctx->module()->execution_modes()) {
		if (mode.opcode() == spv::Op::OpExecutionMode && mode.GetSingleWordInOperand(1) == (uint32_t)spv::ExecutionMode::EarlyFragmentTests) {
			modes.push_back(&mode);
		}
	}
	for (opt::Instruction *mode : modes) {
		p_ctx->KillInst(mode);
	}
	return !modes.empty();
}

bool replace_point_coord(opt::IRContext *p_ctx) {
	std::vector<uint32_t> vars;
	for (opt::Instruction &annotation : p_ctx->annotations()) {
		if (annotation.opcode() == spv::Op::OpDecorate && annotation.GetSingleWordInOperand(1) == (uint32_t)spv::Decoration::BuiltIn &&
				annotation.GetSingleWordInOperand(2) == (uint32_t)spv::BuiltIn::PointCoord) {
			vars.push_back(annotation.GetSingleWordInOperand(0));
		}
	}

	analysis::DefUseManager *du = p_ctx->get_def_use_mgr();
	const uint32_t half = p_ctx->get_constant_mgr()->GetFloatConstId(0.5f);
	bool changed = false;
	for (uint32_t var_id : vars) {
		opt::Instruction *var = du->GetDef(var_id);
		if (!var || var->opcode() != spv::Op::OpVariable) {
			continue;
		}
		const uint32_t value_type = du->GetDef(var->type_id())->GetSingleWordInOperand(1);
		const uint32_t value = splat(p_ctx, value_type, half);

		for (opt::Instruction *user : users_of(p_ctx, var_id)) {
			if (user->opcode() == spv::Op::OpLoad) {
				p_ctx->ReplaceAllUsesWith(user->result_id(), value);
				p_ctx->KillInst(user);
			} else if (user->opcode() == spv::Op::OpAccessChain || user->opcode() == spv::Op::OpInBoundsAccessChain) {
				// gl_PointCoord.x and friends: every component is 0.5.
				for (opt::Instruction *load : users_of(p_ctx, user->result_id())) {
					if (load->opcode() == spv::Op::OpLoad) {
						p_ctx->ReplaceAllUsesWith(load->result_id(), splat(p_ctx, load->type_id(), half));
						p_ctx->KillInst(load);
					}
				}
				p_ctx->KillInst(user);
			}
		}
		replace_in_interfaces(p_ctx, var_id, {});
		p_ctx->KillNamesAndDecorates(var_id);
		p_ctx->KillInst(var);
		changed = true;
	}
	return changed;
}

bool lower_nan_inf(opt::IRContext *p_ctx) {
	analysis::DefUseManager *du = p_ctx->get_def_use_mgr();
	analysis::TypeManager *tm = p_ctx->get_type_mgr();
	analysis::ConstantManager *cm = p_ctx->get_constant_mgr();

	bool changed = false;
	for (opt::Instruction *inst : instructions_with_opcode(p_ctx, { spv::Op::OpIsNan, spv::Op::OpIsInf })) {
		const uint32_t x = inst->GetSingleWordInOperand(0);
		const analysis::Type *x_type = tm->GetType(du->GetDef(x)->type_id());
		uint32_t count = 1;
		const analysis::Float *f = x_type->AsFloat();
		if (const analysis::Vector *vec = x_type->AsVector()) {
			count = vec->element_count();
			f = vec->element_type()->AsFloat();
		}
		if (!f || f->width() != 32) {
			continue;
		}

		// |bits| > exponent mask is NaN, == is infinity.
		const uint32_t uint_type = count == 1 ? tm->GetUIntTypeId() : tm->GetUIntVectorTypeId(count);
		const uint32_t abs_mask = splat(p_ctx, uint_type, cm->GetUIntConstId(0x7fffffffu));
		const uint32_t exponent = splat(p_ctx, uint_type, cm->GetUIntConstId(0x7f800000u));

		opt::InstructionBuilder b(p_ctx, inst, PRESERVED);
		opt::Instruction *bits = b.AddUnaryOp(uint_type, spv::Op::OpBitcast, x);
		opt::Instruction *magnitude = b.AddBinaryOp(uint_type, spv::Op::OpBitwiseAnd, bits->result_id(), abs_mask);
		opt::Instruction *result = b.AddBinaryOp(inst->type_id(), inst->opcode() == spv::Op::OpIsNan ? spv::Op::OpUGreaterThan : spv::Op::OpIEqual,
				magnitude->result_id(), exponent);
		p_ctx->ReplaceAllUsesWith(inst->result_id(), result->result_id());
		p_ctx->KillInst(inst);
		changed = true;
	}
	return changed;
}

bool lower_query_lod(opt::IRContext *p_ctx) {
	analysis::DefUseManager *du = p_ctx->get_def_use_mgr();
	analysis::TypeManager *tm = p_ctx->get_type_mgr();
	analysis::ConstantManager *cm = p_ctx->get_constant_mgr();

	bool changed = false;
	for (opt::Instruction *inst : instructions_with_opcode(p_ctx, { spv::Op::OpImageQueryLod })) {
		const uint32_t sampled_image = inst->GetSingleWordInOperand(0);
		const uint32_t coord = inst->GetSingleWordInOperand(1);
		const uint32_t sampled_image_type = du->GetDef(sampled_image)->type_id();
		const uint32_t image_type = du->GetDef(sampled_image_type)->GetSingleWordInOperand(0);
		const analysis::Image *image = tm->GetType(image_type)->AsImage();
		const uint32_t coord_type = du->GetDef(coord)->type_id();
		const analysis::Vector *coord_vec = tm->GetType(coord_type)->AsVector();
		const uint32_t coord_count = coord_vec ? coord_vec->element_count() : 1;
		if (!image) {
			continue;
		}

		const uint32_t ext = glsl_std_450(p_ctx);
		const uint32_t f32 = tm->GetFloatTypeId();
		const uint32_t i32 = tm->GetSIntTypeId();
		const bool cube = image->dim() == spv::Dim::Cube;
		uint32_t size_count = image->dim() == spv::Dim::Dim1D ? 1 : (image->dim() == spv::Dim::Dim3D ? 3 : 2);
		size_count += image->is_arrayed() ? 1 : 0;
		const uint32_t size_type = size_count == 1 ? i32 : tm->GetSIntVectorTypeId(size_count);

		opt::InstructionBuilder b(p_ctx, inst, PRESERVED);
		const uint32_t img = b.AddUnaryOp(image_type, spv::Op::OpImage, sampled_image)->result_id();
		const uint32_t levels = b.AddUnaryOp(i32, spv::Op::OpImageQueryLevels, img)->result_id();
		const uint32_t size = b.AddBinaryOp(size_type, spv::Op::OpImageQuerySizeLod, img, cm->GetSIntConstId(0))->result_id();
		auto size_component = [&](uint32_t p_index) {
			const uint32_t s = size_count == 1 ? size : b.AddCompositeExtract(i32, size, { p_index })->result_id();
			return b.AddUnaryOp(f32, spv::Op::OpConvertSToF, s)->result_id();
		};

		// The coordinate in texels, whose screen-space derivatives give the footprint.
		uint32_t scaled;
		if (cube) {
			// Project the direction onto its major face, which is size.x texels across.
			const uint32_t a = b.AddNaryExtendedInstruction(coord_type, ext, GLSL_STD_450_FABS, { coord })->result_id();
			const uint32_t ax = b.AddCompositeExtract(f32, a, { 0 })->result_id();
			const uint32_t ay = b.AddCompositeExtract(f32, a, { 1 })->result_id();
			const uint32_t az = b.AddCompositeExtract(f32, a, { 2 })->result_id();
			const uint32_t major = b.AddNaryExtendedInstruction(f32, ext, GLSL_STD_450_FMAX,
											{ b.AddNaryExtendedInstruction(f32, ext, GLSL_STD_450_FMAX, { ax, ay })->result_id(), az })
										   ->result_id();
			const uint32_t half_width = b.AddBinaryOp(f32, spv::Op::OpFMul, size_component(0), cm->GetFloatConstId(0.5f))->result_id();
			const uint32_t scale = b.AddBinaryOp(f32, spv::Op::OpFDiv, half_width, major)->result_id();
			scaled = b.AddBinaryOp(coord_type, spv::Op::OpVectorTimesScalar, coord, scale)->result_id();
		} else {
			uint32_t texels;
			if (coord_count == 1) {
				texels = size_component(0);
			} else {
				std::vector<uint32_t> components;
				for (uint32_t i = 0; i < coord_count; i++) {
					components.push_back(size_component(i));
				}
				texels = b.AddCompositeConstruct(coord_type, components)->result_id();
			}
			scaled = b.AddBinaryOp(coord_type, spv::Op::OpFMul, coord, texels)->result_id();
		}

		const uint32_t dx = b.AddUnaryOp(coord_type, spv::Op::OpDPdx, scaled)->result_id();
		const uint32_t dy = b.AddUnaryOp(coord_type, spv::Op::OpDPdy, scaled)->result_id();
		const spv::Op square = coord_count == 1 ? spv::Op::OpFMul : spv::Op::OpDot;
		const uint32_t dx2 = b.AddBinaryOp(f32, square, dx, dx)->result_id();
		const uint32_t dy2 = b.AddBinaryOp(f32, square, dy, dy)->result_id();
		const uint32_t rho2 = b.AddNaryExtendedInstruction(f32, ext, GLSL_STD_450_FMAX, { dx2, dy2 })->result_id();
		const uint32_t log_rho2 = b.AddNaryExtendedInstruction(f32, ext, GLSL_STD_450_LOG2, { rho2 })->result_id();
		// log2(sqrt(rho2)) == 0.5 * log2(rho2).
		const uint32_t lod = b.AddBinaryOp(f32, spv::Op::OpFMul, log_rho2, cm->GetFloatConstId(0.5f))->result_id();
		const uint32_t max_level = b.AddBinaryOp(f32, spv::Op::OpFSub, b.AddUnaryOp(f32, spv::Op::OpConvertSToF, levels)->result_id(),
											cm->GetFloatConstId(1.0f))
										   ->result_id();
		const uint32_t accessed = b.AddNaryExtendedInstruction(f32, ext, GLSL_STD_450_FCLAMP, { lod, cm->GetFloatConstId(0.0f), max_level })->result_id();
		opt::Instruction *result = b.AddCompositeConstruct(inst->type_id(), { accessed, lod });

		p_ctx->ReplaceAllUsesWith(inst->result_id(), result->result_id());
		p_ctx->KillInst(inst);
		changed = true;
	}
	return changed;
}

// ---- expand_binding_arrays ----

bool is_handle_type(spv::Op p_op) {
	return p_op == spv::Op::OpTypeImage || p_op == spv::Op::OpTypeSampler;
}

// Instructions that consume a texture/sampler/storage image and produce an
// ordinary value (or nothing). These are what get cloned into each switch case.
bool is_handle_consumer(spv::Op p_op) {
	switch (p_op) {
		case spv::Op::OpImageSampleImplicitLod:
		case spv::Op::OpImageSampleExplicitLod:
		case spv::Op::OpImageSampleDrefImplicitLod:
		case spv::Op::OpImageSampleDrefExplicitLod:
		case spv::Op::OpImageSampleProjImplicitLod:
		case spv::Op::OpImageSampleProjExplicitLod:
		case spv::Op::OpImageSampleProjDrefImplicitLod:
		case spv::Op::OpImageSampleProjDrefExplicitLod:
		case spv::Op::OpImageFetch:
		case spv::Op::OpImageGather:
		case spv::Op::OpImageDrefGather:
		case spv::Op::OpImageRead:
		case spv::Op::OpImageWrite:
		case spv::Op::OpImageQueryFormat:
		case spv::Op::OpImageQueryOrder:
		case spv::Op::OpImageQuerySizeLod:
		case spv::Op::OpImageQuerySize:
		case spv::Op::OpImageQueryLod:
		case spv::Op::OpImageQueryLevels:
		case spv::Op::OpImageQuerySamples:
			return true;
		default:
			return false;
	}
}

// Instructions that only pass a handle along: these are rebuilt per case.
bool is_handle_carrier(spv::Op p_op) {
	return p_op == spv::Op::OpLoad || p_op == spv::Op::OpSampledImage || p_op == spv::Op::OpImage || p_op == spv::Op::OpCopyObject;
}

// What a pipeline counts against WebGPU's per-stage limits: each handle
// binding the stage statically uses. A module that is never expanded keeps
// each array as one binding (flatten_binding_arrays() collapses it).
enum HandleKind {
	HANDLE_SAMPLED_TEXTURE,
	HANDLE_STORAGE_TEXTURE,
	HANDLE_SAMPLER,
	HANDLE_KIND_MAX,
};

struct ArrayVar {
	uint32_t var_id = 0;
	HandleKind kind = HANDLE_SAMPLED_TEXTURE;
	uint32_t element_pointer_type = 0;
	uint32_t length = 0;
	uint32_t glsl_binding = 0;
	std::string name;
	std::vector<opt::Instruction *> access_chains;
};

struct Consumer {
	opt::Instruction *inst = nullptr;
	opt::Instruction *access_chain = nullptr;
};

// Every handle-carrying value derived from p_access_chain, and the consumers
// that end each chain. Returns false for a use the expansion cannot rewrite.
bool collect_chain(opt::IRContext *p_ctx, opt::Instruction *p_access_chain, std::unordered_set<uint32_t> &r_carriers, std::vector<Consumer> &r_consumers) {
	std::vector<uint32_t> work = { p_access_chain->result_id() };
	while (!work.empty()) {
		const uint32_t id = work.back();
		work.pop_back();
		for (opt::Instruction *user : users_of(p_ctx, id)) {
			const spv::Op op = user->opcode();
			if (op == spv::Op::OpName || op == spv::Op::OpDecorate) {
				continue;
			}
			// Only the access chain itself may be loaded; a handle value is never a pointer.
			if (op == spv::Op::OpLoad && id != p_access_chain->result_id()) {
				return false;
			}
			if (is_handle_carrier(op)) {
				if (r_carriers.insert(user->result_id()).second) {
					work.push_back(user->result_id());
				}
			} else if (is_handle_consumer(op)) {
				bool listed = false;
				for (const Consumer &c : r_consumers) {
					listed = listed || c.inst == user;
				}
				if (!listed) {
					r_consumers.push_back({ user, p_access_chain });
				}
			} else {
				// OpPhi, OpSelect, OpStore, OpFunctionCall, ...: a handle that
				// escapes its chain cannot be rebuilt inside a switch case.
				return false;
			}
		}
	}
	return true;
}

// A switch case may be added before p_consumer only when its block is not a
// loop header: splitting one would move the OpLoopMerge away from the block
// the back edge targets.
bool can_split_before(opt::IRContext *p_ctx, opt::Instruction *p_consumer) {
	opt::BasicBlock *block = p_ctx->get_instr_block(p_consumer);
	return block && block->GetLoopMergeInst() == nullptr;
}

// Re-emits the handle chain feeding p_id for array element p_element, at the builder's position.
uint32_t rebuild_chain(opt::IRContext *p_ctx, opt::InstructionBuilder &p_builder, uint32_t p_id, uint32_t p_element_var,
		const std::unordered_set<uint32_t> &p_carriers, std::unordered_map<uint32_t, uint32_t> &r_remap) {
	if (!p_carriers.count(p_id)) {
		return p_id;
	}
	auto found = r_remap.find(p_id);
	if (found != r_remap.end()) {
		return found->second;
	}
	opt::Instruction *def = p_ctx->get_def_use_mgr()->GetDef(p_id);
	uint32_t result = 0;
	switch (def->opcode()) {
		case spv::Op::OpLoad:
			result = p_builder.AddLoad(def->type_id(), p_element_var)->result_id();
			break;
		case spv::Op::OpSampledImage:
			result = p_builder.AddSampledImage(def->type_id(),
									  rebuild_chain(p_ctx, p_builder, def->GetSingleWordInOperand(0), p_element_var, p_carriers, r_remap),
									  rebuild_chain(p_ctx, p_builder, def->GetSingleWordInOperand(1), p_element_var, p_carriers, r_remap))
							 ->result_id();
			break;
		case spv::Op::OpImage:
			result = p_builder.AddUnaryOp(def->type_id(), spv::Op::OpImage,
									  rebuild_chain(p_ctx, p_builder, def->GetSingleWordInOperand(0), p_element_var, p_carriers, r_remap))
							 ->result_id();
			break;
		case spv::Op::OpCopyObject:
			result = rebuild_chain(p_ctx, p_builder, def->GetSingleWordInOperand(0), p_element_var, p_carriers, r_remap);
			break;
		default:
			return p_id;
	}
	r_remap[p_id] = result;
	return result;
}

// Replaces p_consumer, which reads array p_array at a dynamic index, with
//
//   OpSelectionMerge %tail
//   OpSwitch %index %case_0 1 %case_1 ... (N-1) %case_N-1
//   %case_k:  <chain rebuilt from element k> <clone of p_consumer>; OpBranch %tail
//   %tail:    %result = OpPhi <clone results>; <the rest of the original block>
void split_into_switch(opt::IRContext *p_ctx, const Consumer &p_consumer, const std::vector<uint32_t> &p_element_vars,
		const std::unordered_set<uint32_t> &p_carriers) {
	opt::Instruction *inst = p_consumer.inst;
	const uint32_t index = p_consumer.access_chain->GetSingleWordInOperand(1);
	opt::BasicBlock *head = p_ctx->get_instr_block(inst);
	opt::Function *function = head->GetParent();

	const uint32_t tail_label = p_ctx->TakeNextId();
	head->SplitBasicBlock(p_ctx, tail_label, opt::BasicBlock::iterator(inst));

	std::vector<uint32_t> case_labels;
	std::vector<uint32_t> phi_operands;
	opt::BasicBlock *insert_after = head;
	for (uint32_t k = 0; k < p_element_vars.size(); k++) {
		const uint32_t label = p_ctx->TakeNextId();
		std::unique_ptr<opt::BasicBlock> block = std::make_unique<opt::BasicBlock>(
				std::make_unique<opt::Instruction>(p_ctx, spv::Op::OpLabel, 0, label, std::initializer_list<opt::Operand>{}));
		opt::BasicBlock *case_block = function->InsertBasicBlockAfter(std::move(block), insert_after);
		insert_after = case_block;
		p_ctx->AnalyzeDefUse(case_block->GetLabelInst());
		p_ctx->set_instr_block(case_block->GetLabelInst(), case_block);

		opt::InstructionBuilder b(p_ctx, case_block, PRESERVED);
		std::unordered_map<uint32_t, uint32_t> remap;
		std::unique_ptr<opt::Instruction> clone(inst->Clone(p_ctx));
		if (inst->HasResultId()) {
			clone->SetResultId(p_ctx->TakeNextId());
		}
		for (uint32_t i = 0; i < clone->NumInOperands(); i++) {
			const opt::Operand &operand = clone->GetInOperand(i);
			if (operand.type == SPV_OPERAND_TYPE_ID) {
				clone->SetInOperand(i, { rebuild_chain(p_ctx, b, operand.words[0], p_element_vars[k], p_carriers, remap) });
			}
		}
		opt::Instruction *cloned = b.AddInstruction(std::move(clone));
		b.AddBranch(tail_label);

		case_labels.push_back(label);
		if (inst->HasResultId()) {
			phi_operands.push_back(cloned->result_id());
			phi_operands.push_back(label);
		}
	}

	std::vector<std::pair<opt::Operand::OperandData, uint32_t>> targets;
	for (uint32_t k = 1; k < case_labels.size(); k++) {
		targets.push_back({ { k }, case_labels[k] });
	}
	// Out-of-range indices are undefined behavior in GLSL; they take element 0.
	opt::InstructionBuilder head_builder(p_ctx, head, PRESERVED);
	head_builder.AddSwitch(index, case_labels[0], targets, tail_label);

	if (inst->HasResultId()) {
		opt::InstructionBuilder tail_builder(p_ctx, inst, PRESERVED);
		opt::Instruction *phi = tail_builder.AddPhi(inst->type_id(), phi_operands);
		p_ctx->ReplaceAllUsesWith(inst->result_id(), phi->result_id());
	}
	p_ctx->KillInst(inst);
}

void kill_if_unused(opt::IRContext *p_ctx, uint32_t p_id) {
	opt::Instruction *def = p_ctx->get_def_use_mgr()->GetDef(p_id);
	if (!def) {
		return;
	}
	bool used = false;
	p_ctx->get_def_use_mgr()->ForEachUser(p_id, [&used](opt::Instruction *p_user) {
		used = used || !is_debug_or_annotation(p_user->opcode());
	});
	if (used) {
		return;
	}
	std::vector<uint32_t> operands;
	def->ForEachInId([&operands](const uint32_t *p_operand) { operands.push_back(*p_operand); });
	p_ctx->KillNamesAndDecorates(p_id);
	p_ctx->KillInst(def);
	for (uint32_t operand : operands) {
		opt::Instruction *operand_def = p_ctx->get_def_use_mgr()->GetDef(operand);
		if (operand_def && (is_handle_carrier(operand_def->opcode()) || operand_def->opcode() == spv::Op::OpAccessChain || operand_def->opcode() == spv::Op::OpInBoundsAccessChain)) {
			kill_if_unused(p_ctx, operand);
		}
	}
}

// The element type of a handle variable or handle-array variable, or 0.
uint32_t handle_element_type(opt::IRContext *p_ctx, opt::Instruction &p_var) {
	analysis::DefUseManager *du = p_ctx->get_def_use_mgr();
	if (p_var.opcode() != spv::Op::OpVariable || p_var.GetSingleWordInOperand(0) != (uint32_t)spv::StorageClass::UniformConstant) {
		return 0;
	}
	opt::Instruction *type = du->GetDef(du->GetDef(p_var.type_id())->GetSingleWordInOperand(1));
	while (type && (type->opcode() == spv::Op::OpTypeArray || type->opcode() == spv::Op::OpTypeRuntimeArray)) {
		type = du->GetDef(type->GetSingleWordInOperand(0));
	}
	return type && is_handle_type(type->opcode()) ? type->result_id() : 0;
}

HandleKind handle_kind(opt::IRContext *p_ctx, uint32_t p_element_type) {
	opt::Instruction *type = p_ctx->get_def_use_mgr()->GetDef(p_element_type);
	if (type->opcode() == spv::Op::OpTypeSampler) {
		return HANDLE_SAMPLER;
	}
	// OpTypeImage in-operand 5 is Sampled: 2 means a storage image.
	return type->GetSingleWordInOperand(5) == 2 ? HANDLE_STORAGE_TEXTURE : HANDLE_SAMPLED_TEXTURE;
}

bool is_referenced(opt::IRContext *p_ctx, uint32_t p_id) {
	bool referenced = false;
	p_ctx->get_def_use_mgr()->ForEachUser(p_id, [&referenced](opt::Instruction *p_user) {
		referenced = referenced || !is_debug_or_annotation(p_user->opcode());
	});
	return referenced;
}

// How many bindings of each kind the module may use after expansion.
//
// WebGPU only guarantees 16 sampled textures, 4 storage textures and 16
// samplers per stage, and expanding past what an adapter offers turns a
// shader whose array reads were collapsed into one whose pipeline cannot be
// created at all. A module already over the sampled-texture default only runs
// where Godot runs Forward+, and RendererCompositorRD::initialize() only picks
// Forward+ on adapters with at least 48 sampled textures per stage (Chrome on
// desktop reports exactly 48), so such modules may grow to that. Storage
// textures get the same treatment with the 8 every desktop adapter reports.
void handle_budget(const uint32_t p_used[HANDLE_KIND_MAX], uint32_t r_budget[HANDLE_KIND_MAX]) {
	r_budget[HANDLE_SAMPLED_TEXTURE] = p_used[HANDLE_SAMPLED_TEXTURE] > 16 ? 48 : 16;
	r_budget[HANDLE_STORAGE_TEXTURE] = p_used[HANDLE_STORAGE_TEXTURE] > 4 ? 8 : 4;
	r_budget[HANDLE_SAMPLER] = 16;
}

bool find_array_var(opt::IRContext *p_ctx, opt::Instruction &p_var, ArrayVar &r_array) {
	analysis::DefUseManager *du = p_ctx->get_def_use_mgr();
	if (p_var.opcode() != spv::Op::OpVariable || p_var.GetSingleWordInOperand(0) != (uint32_t)spv::StorageClass::UniformConstant) {
		return false;
	}
	opt::Instruction *array = du->GetDef(du->GetDef(p_var.type_id())->GetSingleWordInOperand(1));
	if (!array || array->opcode() != spv::Op::OpTypeArray) {
		return false;
	}
	const uint32_t element_type = array->GetSingleWordInOperand(0);
	opt::Instruction *element = du->GetDef(element_type);
	opt::Instruction *length = du->GetDef(array->GetSingleWordInOperand(1));
	if (!element || !is_handle_type(element->opcode()) || !length || length->opcode() != spv::Op::OpConstant) {
		return false;
	}

	bool has_binding = false;
	bool read_only = false;
	bool write_only = false;
	for (opt::Instruction *decoration : p_ctx->get_decoration_mgr()->GetDecorationsFor(p_var.result_id(), false)) {
		if (decoration->opcode() == spv::Op::OpDecorate) {
			read_only = read_only || decoration->GetSingleWordInOperand(1) == (uint32_t)spv::Decoration::NonWritable;
			write_only = write_only || decoration->GetSingleWordInOperand(1) == (uint32_t)spv::Decoration::NonReadable;
		}
		if (decoration->opcode() == spv::Op::OpDecorate && decoration->GetSingleWordInOperand(1) == (uint32_t)spv::Decoration::Binding) {
			const uint32_t binding = decoration->GetSingleWordInOperand(2);
			// Bindings are doubled by split_combined_samplers() by now; an odd
			// one is the sampler half of a combined image sampler.
			if (binding % 2 != 0) {
				return false;
			}
			r_array.glsl_binding = binding / 2;
			has_binding = true;
		}
	}
	r_array.var_id = p_var.result_id();
	r_array.kind = handle_kind(p_ctx, element_type);
	// A read_write storage image may be split by the driver into a write
	// binding and a read shadow at the next binding number, which is where the
	// next expanded element would sit.
	if (r_array.kind == HANDLE_STORAGE_TEXTURE && !read_only && !write_only) {
		return false;
	}
	r_array.length = length->GetSingleWordInOperand(0);
	if (!has_binding || !spirv_binding_layout::array_can_expand(r_array.glsl_binding, r_array.length)) {
		return false;
	}
	r_array.element_pointer_type = p_ctx->get_type_mgr()->FindPointerToType(element_type, spv::StorageClass::UniformConstant);

	for (opt::Instruction *user : users_of(p_ctx, r_array.var_id)) {
		if (is_debug_or_annotation(user->opcode())) {
			if (user->opcode() == spv::Op::OpName) {
				r_array.name = utils::MakeString(user->GetInOperand(1).words);
			}
			continue;
		}
		const bool access_chain = user->opcode() == spv::Op::OpAccessChain || user->opcode() == spv::Op::OpInBoundsAccessChain;
		if (!access_chain || user->NumInOperands() != 2) {
			return false;
		}
		r_array.access_chains.push_back(user);
	}
	return true;
}

} // namespace

bool lower_wgsl_unsupported_ops(std::vector<uint32_t> &r_words) {
	bool needed = false;
	for_each_raw_instruction(r_words, [&needed](spv::Op p_op, const uint32_t *p_inst, uint32_t p_wc) {
		needed = needed || p_op == spv::Op::OpIsNan || p_op == spv::Op::OpIsInf || p_op == spv::Op::OpImageQueryLod ||
				(p_op == spv::Op::OpExecutionMode && p_wc >= 3 && p_inst[2] == (uint32_t)spv::ExecutionMode::EarlyFragmentTests) ||
				(p_op == spv::Op::OpDecorate && p_wc >= 4 && p_inst[2] == (uint32_t)spv::Decoration::BuiltIn && p_inst[3] == (uint32_t)spv::BuiltIn::PointCoord);
		return !needed;
	});
	if (!needed) {
		return false;
	}
	std::unique_ptr<opt::IRContext> ctx = build(r_words);
	if (!ctx) {
		return false;
	}
	bool changed = strip_early_fragment_tests(ctx.get());
	changed = replace_point_coord(ctx.get()) || changed;
	changed = lower_nan_inf(ctx.get()) || changed;
	changed = lower_query_lod(ctx.get()) || changed;
	if (changed) {
		emit(ctx.get(), r_words);
	}
	return changed;
}

bool expand_binding_arrays(std::vector<uint32_t> &r_words) {
	std::unordered_set<uint32_t> handle_types;
	bool needed = false;
	for_each_raw_instruction(r_words, [&](spv::Op p_op, const uint32_t *p_inst, uint32_t p_wc) {
		if ((p_op == spv::Op::OpTypeImage || p_op == spv::Op::OpTypeSampler) && p_wc >= 2) {
			handle_types.insert(p_inst[1]);
		} else if (p_op == spv::Op::OpTypeArray && p_wc >= 4 && handle_types.count(p_inst[2])) {
			needed = true;
		}
		return !needed;
	});
	if (!needed) {
		return false;
	}
	std::unique_ptr<opt::IRContext> ctx = build(r_words);
	if (!ctx) {
		return false;
	}
	opt::IRContext *c = ctx.get();

	// Collected first: find_array_var() may declare a pointer type, which
	// appends to the very list being walked.
	std::vector<opt::Instruction *> globals;
	for (opt::Instruction &inst : c->types_values()) {
		if (inst.opcode() == spv::Op::OpVariable) {
			globals.push_back(&inst);
		}
	}
	uint32_t used[HANDLE_KIND_MAX] = {};
	std::vector<ArrayVar> arrays;
	for (opt::Instruction *var : globals) {
		const uint32_t element_type = handle_element_type(c, *var);
		if (element_type && is_referenced(c, var->result_id())) {
			used[handle_kind(c, element_type)]++;
		}
		ArrayVar array;
		if (find_array_var(c, *var, array)) {
			arrays.push_back(std::move(array));
		}
	}
	uint32_t budget[HANDLE_KIND_MAX];
	handle_budget(used, budget);

	bool changed = false;
	for (ArrayVar &array : arrays) {
		// Validate every use before touching anything, so an array is either
		// fully expanded or left exactly as it was.
		std::unordered_set<uint32_t> carriers;
		std::vector<Consumer> consumers;
		bool supported = true;
		for (opt::Instruction *access_chain : array.access_chains) {
			opt::Instruction *index = c->get_def_use_mgr()->GetDef(access_chain->GetSingleWordInOperand(1));
			if (index->opcode() == spv::Op::OpConstant) {
				continue;
			}
			const analysis::Integer *index_type = c->get_type_mgr()->GetType(index->type_id())->AsInteger();
			supported = supported && index_type && index_type->width() == 32 && collect_chain(c, access_chain, carriers, consumers);
		}
		for (const Consumer &consumer : consumers) {
			supported = supported && can_split_before(c, consumer.inst);
		}
		// Declaration order decides which arrays fit: in Forward+'s scene
		// shader that is the lightmaps before VoxelGI's forward-GI path.
		const uint32_t extra = is_referenced(c, array.var_id) ? array.length - 1 : 0;
		if (!supported || used[array.kind] + extra > budget[array.kind]) {
			continue;
		}
		used[array.kind] += extra;

		std::vector<uint32_t> element_vars;
		for (uint32_t k = 0; k < array.length; k++) {
			const uint32_t id = c->TakeNextId();
			c->AddGlobalValue(std::make_unique<opt::Instruction>(c, spv::Op::OpVariable, array.element_pointer_type, id,
					std::initializer_list<opt::Operand>{ { SPV_OPERAND_TYPE_STORAGE_CLASS, { (uint32_t)spv::StorageClass::UniformConstant } } }));
			c->get_decoration_mgr()->CloneDecorations(array.var_id, id);
			for (opt::Instruction *decoration : c->get_decoration_mgr()->GetDecorationsFor(id, false)) {
				if (decoration->opcode() == spv::Op::OpDecorate && decoration->GetSingleWordInOperand(1) == (uint32_t)spv::Decoration::Binding) {
					decoration->SetInOperand(2, { spirv_binding_layout::array_element_binding(array.glsl_binding, k) });
					c->UpdateDefUse(decoration);
				}
			}
			if (!array.name.empty()) {
				c->AddDebug2Inst(std::make_unique<opt::Instruction>(c, spv::Op::OpName, 0, 0,
						std::initializer_list<opt::Operand>{ { SPV_OPERAND_TYPE_ID, { id } },
								{ SPV_OPERAND_TYPE_LITERAL_STRING, utils::MakeVector(array.name + "_" + std::to_string(k)) } }));
			}
			element_vars.push_back(id);
		}
		replace_in_interfaces(c, array.var_id, element_vars);

		for (const Consumer &consumer : consumers) {
			split_into_switch(c, consumer, element_vars, carriers);
		}
		for (opt::Instruction *access_chain : array.access_chains) {
			opt::Instruction *index = c->get_def_use_mgr()->GetDef(access_chain->GetSingleWordInOperand(1));
			const uint32_t ac_id = access_chain->result_id();
			if (index->opcode() == spv::Op::OpConstant) {
				const uint32_t k = std::min(index->GetSingleWordInOperand(0), array.length - 1);
				c->ReplaceAllUsesWith(ac_id, element_vars[k]);
				c->KillInst(access_chain);
			} else {
				// Its loads are dead now that every consumer reads an element variable.
				for (opt::Instruction *load : users_of(c, ac_id)) {
					if (load->opcode() == spv::Op::OpLoad) {
						kill_if_unused(c, load->result_id());
					}
				}
				kill_if_unused(c, ac_id);
			}
		}
		// Carriers built on top of those loads (sampled images, OpImage) went
		// with them; anything still left is unused too.
		for (uint32_t carrier : carriers) {
			kill_if_unused(c, carrier);
		}
		c->KillNamesAndDecorates(array.var_id);
		c->KillInst(c->get_def_use_mgr()->GetDef(array.var_id));
		changed = true;
	}

	if (changed) {
		emit(c, r_words);
	}
	return changed;
}

bool apply_sampler_lod_bias(std::vector<uint32_t> &r_words) {
	// Cheap test first: is the bias buffer declared at all?
	bool needed = false;
	for_each_raw_instruction(r_words, [&needed](spv::Op p_op, const uint32_t *p_inst, uint32_t p_wc) {
		needed = needed || (p_op == spv::Op::OpDecorate && p_wc >= 4 && p_inst[2] == (uint32_t)spv::Decoration::Binding && p_inst[3] == spirv_binding_layout::SAMPLER_LOD_BIAS_GLSL_BINDING * 2);
		return !needed;
	});
	if (!needed) {
		return false;
	}
	std::unique_ptr<opt::IRContext> ctx = build(r_words);
	if (!ctx) {
		return false;
	}
	opt::IRContext *c = ctx.get();
	analysis::DefUseManager *du = c->get_def_use_mgr();
	analysis::DecorationManager *dm = c->get_decoration_mgr();
	analysis::TypeManager *tm = c->get_type_mgr();
	analysis::ConstantManager *cm = c->get_constant_mgr();

	auto decoration_value = [dm](uint32_t p_id, spv::Decoration p_decoration, uint32_t &r_value) {
		for (opt::Instruction *d : dm->GetDecorationsFor(p_id, false)) {
			if (d->opcode() == spv::Op::OpDecorate && d->GetSingleWordInOperand(1) == (uint32_t)p_decoration) {
				r_value = d->GetSingleWordInOperand(2);
				return true;
			}
		}
		return false;
	};

	uint32_t bias_var = 0;
	uint32_t bias_set = 0;
	for (opt::Instruction &inst : c->types_values()) {
		uint32_t binding = 0;
		if (inst.opcode() == spv::Op::OpVariable && inst.GetSingleWordInOperand(0) == (uint32_t)spv::StorageClass::Uniform &&
				decoration_value(inst.result_id(), spv::Decoration::Binding, binding) && binding == spirv_binding_layout::SAMPLER_LOD_BIAS_GLSL_BINDING * 2 &&
				decoration_value(inst.result_id(), spv::Decoration::DescriptorSet, bias_set)) {
			bias_var = inst.result_id();
			break;
		}
	}
	if (bias_var == 0) {
		return false;
	}

	// The sampler a sample instruction reads through, if it is loaded straight
	// from a sampler variable of the bias buffer's set.
	auto biased_sampler = [&](opt::Instruction *p_sample) {
		opt::Instruction *sampled_image = du->GetDef(p_sample->GetSingleWordInOperand(0));
		if (!sampled_image || sampled_image->opcode() != spv::Op::OpSampledImage) {
			return false;
		}
		opt::Instruction *load = du->GetDef(sampled_image->GetSingleWordInOperand(1));
		if (!load || load->opcode() != spv::Op::OpLoad) {
			return false;
		}
		opt::Instruction *var = du->GetDef(load->GetSingleWordInOperand(0));
		uint32_t set = 0;
		return var && var->opcode() == spv::Op::OpVariable && decoration_value(var->result_id(), spv::Decoration::DescriptorSet, set) && set == bias_set;
	};

	const uint32_t f32 = tm->GetFloatTypeId();
	const uint32_t float_ptr = tm->FindPointerToType(f32, spv::StorageClass::Uniform);
	const uint32_t member0 = cm->GetSIntConstId(0);

	bool changed = false;
	for (opt::Instruction *sample : instructions_with_opcode(c, { spv::Op::OpImageSampleImplicitLod, spv::Op::OpImageSampleProjImplicitLod })) {
		if (!biased_sampler(sample)) {
			continue;
		}
		opt::InstructionBuilder b(c, sample, PRESERVED);
		const uint32_t bias = b.AddLoad(f32, b.AddAccessChain(float_ptr, bias_var, { member0 })->result_id())->result_id();

		// In-operands: sampled image, coordinate, then an optional image-operands
		// mask and its operands, in mask-bit order -- Bias (0x1) comes first.
		std::vector<opt::Operand> operands;
		for (uint32_t i = 0; i < 2; i++) {
			operands.push_back(sample->GetInOperand(i));
		}
		const uint32_t mask = sample->NumInOperands() > 2 ? sample->GetSingleWordInOperand(2) : 0;
		uint32_t next = 3;
		operands.push_back(opt::Operand(SPV_OPERAND_TYPE_IMAGE, { mask | (uint32_t)spv::ImageOperandsMask::Bias }));
		if (mask & (uint32_t)spv::ImageOperandsMask::Bias) {
			// The shader's own bias (texture(s, uv, bias)) adds to the sampler's.
			const uint32_t own = sample->GetSingleWordInOperand(next++);
			operands.push_back(opt::Operand(SPV_OPERAND_TYPE_ID, { b.AddBinaryOp(f32, spv::Op::OpFAdd, own, bias)->result_id() }));
		} else {
			operands.push_back(opt::Operand(SPV_OPERAND_TYPE_ID, { bias }));
		}
		for (uint32_t i = next; i < sample->NumInOperands(); i++) {
			operands.push_back(sample->GetInOperand(i));
		}
		sample->SetInOperands(std::move(operands));
		c->UpdateDefUse(sample);
		changed = true;
	}
	if (!changed) {
		return false;
	}

	// SPIR-V 1.4+ entry points list every global they use; the buffer was
	// declared but never read until now. (Up to 1.3 they list only inputs and
	// outputs, and listing it would be invalid.)
	const uint32_t version = c->module()->version();
	for (opt::Instruction &ep : c->module()->entry_points()) {
		if (version < 0x00010400u) {
			break;
		}
		bool listed = false;
		for (uint32_t i = ENTRY_POINT_FIRST_INTERFACE; i < ep.NumInOperands(); i++) {
			listed = listed || ep.GetSingleWordInOperand(i) == bias_var;
		}
		if (!listed) {
			ep.AddOperand(opt::Operand(SPV_OPERAND_TYPE_ID, { bias_var }));
			c->UpdateDefUse(&ep);
		}
	}
	emit(c, r_words);
	return true;
}

bool validate_module(const uint32_t *p_words, size_t p_word_count, std::string *r_error) {
	if (p_words == nullptr || p_word_count < 5) {
		if (r_error) {
			*r_error = "not a SPIR-V module (shorter than its header)";
		}
		return false;
	}
	SpirvTools tools(SPV_ENV_VULKAN_1_2);
	std::string first_error;
	tools.SetMessageConsumer([&first_error](spv_message_level_t p_level, const char *, const spv_position_t &p_position, const char *p_message) {
		if (first_error.empty() && p_level <= SPV_MSG_ERROR) {
			first_error = std::string(p_message) + " (word " + std::to_string(p_position.index) + ")";
		}
	});
	if (tools.Validate(p_words, p_word_count, ValidatorOptions())) {
		return true;
	}
	if (r_error) {
		*r_error = first_error.empty() ? std::string("rejected by the SPIR-V validator") : first_error;
		// The validator appends the offending instruction's disassembly on
		// lines of its own; callers log this as one line.
		std::string one_line;
		for (char c : *r_error) {
			if (c == '\n') {
				c = ' ';
			}
			if (c != ' ' || (!one_line.empty() && one_line.back() != ' ')) {
				one_line += c;
			}
		}
		*r_error = one_line;
	}
	return false;
}

namespace {

struct PreconditionScan {
	uint32_t bound = 0;
	// Result id -> opcode, for every instruction with a result.
	std::unordered_map<uint32_t, uint16_t> defs;
	// Every id operand, checked against defs once the whole module is seen
	// (SPIR-V allows forward references: decorations, names, OpPhi, calls).
	std::vector<uint32_t> uses;
	// Ids that must be an OpFunction: OpEntryPoint's and OpFunctionCall's.
	std::vector<uint32_t> functions_needed;
	bool in_function = false;
	bool in_block = false;
	std::string error;
};

// IRContext hands out no id at or past 0x3FFFFF (its default max id bound,
// also the validator's): TakeNextId() returns 0 instead, which no pass here
// checks, and a variable with result id 0 is a use of an undefined id the next
// analysis asserts on. Half of that leaves every pass room for the ids it adds.
constexpr uint32_t MAX_ID_BOUND = 0x3FFFFF / 2;

spv_result_t precondition_header(void *p_user, spv_endianness_t, uint32_t, uint32_t, uint32_t, uint32_t p_bound, uint32_t) {
	PreconditionScan &scan = *static_cast<PreconditionScan *>(p_user);
	scan.bound = p_bound;
	if (p_bound > MAX_ID_BOUND) {
		scan.error = "id bound " + std::to_string(p_bound) + " is over " + std::to_string(MAX_ID_BOUND);
		return SPV_ERROR_INVALID_BINARY;
	}
	return SPV_SUCCESS;
}

spv_result_t precondition_instruction(void *p_user, const spv_parsed_instruction_t *p_inst) {
	PreconditionScan &scan = *static_cast<PreconditionScan *>(p_user);
	const spv::Op op = (spv::Op)p_inst->opcode;
	auto fail = [&scan, op](const std::string &p_what) {
		scan.error = p_what + " (at an Op" + spvOpcodeString(op) + ")";
		return SPV_ERROR_INVALID_BINARY;
	};

	if (p_inst->result_id != 0) {
		if (p_inst->result_id >= scan.bound) {
			return fail("result id " + std::to_string(p_inst->result_id) + " is not below the header's bound " + std::to_string(scan.bound));
		}
		if (!scan.defs.emplace(p_inst->result_id, p_inst->opcode).second) {
			return fail("result id " + std::to_string(p_inst->result_id) + " is defined twice");
		}
	}
	for (uint16_t i = 0; i < p_inst->num_operands; i++) {
		const spv_parsed_operand_t &operand = p_inst->operands[i];
		switch (operand.type) {
			case SPV_OPERAND_TYPE_ID:
			case SPV_OPERAND_TYPE_TYPE_ID:
			case SPV_OPERAND_TYPE_MEMORY_SEMANTICS_ID:
			case SPV_OPERAND_TYPE_SCOPE_ID:
				scan.uses.push_back(p_inst->words[operand.offset]);
				break;
			default:
				break;
		}
	}

	switch (op) {
		case spv::Op::OpEntryPoint:
			if (p_inst->num_words < 3) {
				return fail("truncated instruction");
			}
			scan.functions_needed.push_back(p_inst->words[2]);
			break;
		case spv::Op::OpFunctionCall:
			if (p_inst->num_words < 4) {
				return fail("truncated instruction");
			}
			scan.functions_needed.push_back(p_inst->words[3]);
			break;
		case spv::Op::OpFunction:
			if (scan.in_function) {
				return fail("function inside a function");
			}
			scan.in_function = true;
			break;
		case spv::Op::OpFunctionEnd:
			if (!scan.in_function) {
				return fail("OpFunctionEnd outside a function");
			}
			if (scan.in_block) {
				return fail("basic block without a terminator");
			}
			scan.in_function = false;
			break;
		case spv::Op::OpLabel:
			if (!scan.in_function) {
				return fail("OpLabel outside a function");
			}
			if (scan.in_block) {
				return fail("basic block without a terminator");
			}
			scan.in_block = true;
			break;
		default:
			if (spvOpcodeIsBlockTerminator(op)) {
				if (!scan.in_block) {
					return fail("terminator outside a basic block");
				}
				scan.in_block = false;
			}
			break;
	}
	return SPV_SUCCESS;
}

} // namespace

bool check_ir_preconditions(const uint32_t *p_words, size_t p_word_count, std::string *r_error) {
	if (p_words == nullptr || p_word_count < 5) {
		if (r_error) {
			*r_error = "not a SPIR-V module (shorter than its header)";
		}
		return false;
	}
	PreconditionScan scan;
	spv_context context = spvContextCreate(SPV_ENV_VULKAN_1_2);
	spv_diagnostic diagnostic = nullptr;
	const spv_result_t result = spvBinaryParse(context, &scan, p_words, p_word_count, precondition_header, precondition_instruction, &diagnostic);
	if (result != SPV_SUCCESS && scan.error.empty()) {
		scan.error = diagnostic && diagnostic->error ? diagnostic->error : "unparsable instruction stream";
	}
	spvDiagnosticDestroy(diagnostic);
	spvContextDestroy(context);

	if (scan.error.empty() && scan.in_function) {
		scan.error = "function without OpFunctionEnd";
	}
	for (size_t i = 0; scan.error.empty() && i < scan.uses.size(); i++) {
		if (!scan.defs.count(scan.uses[i])) {
			scan.error = "id " + std::to_string(scan.uses[i]) + " is used but never defined";
		}
	}
	for (size_t i = 0; scan.error.empty() && i < scan.functions_needed.size(); i++) {
		auto def = scan.defs.find(scan.functions_needed[i]);
		if (def == scan.defs.end() || def->second != (uint16_t)spv::Op::OpFunction) {
			scan.error = "id " + std::to_string(scan.functions_needed[i]) + " is called or used as an entry point but is not a function";
		}
	}
	if (scan.error.empty()) {
		return true;
	}
	if (r_error) {
		*r_error = scan.error;
	}
	return false;
}

} // namespace spirv_opt_passes
