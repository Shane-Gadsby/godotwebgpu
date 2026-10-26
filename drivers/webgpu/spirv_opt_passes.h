/**************************************************************************/
/*  spirv_opt_passes.h                                                    */
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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// SPIR-V preprocessing passes that restructure control flow or add types,
// written against SPIRV-Tools' optimizer IR (def-use chains, basic blocks,
// type and constant managers) rather than as raw word rewrites like the passes
// in spirv_preprocess.cpp. Kept in a file of their own because SPIRV-Tools'
// internal headers need include paths the rest of the driver does not get.
//
// Each pass rewrites r_words in place and returns whether it changed anything.
// A module the IR loader cannot parse, or that fails check_ir_preconditions(),
// is left untouched.
namespace spirv_opt_passes {

// Rewrites constructs WGSL has no direct equivalent for into ones it does.
// Written for Tint, the converter before SPIRV-Cross, whose SPIR-V reader
// aborted on them (taking down the whole wasm instance); the pass still runs
// ahead of SPIRV-Cross and keeps its name:
//  - OpIsNan / OpIsInf (GLSL isnan()/isinf(), which Godot's shading language
//    exposes): compared on the float's bits, so a NaN is still found under
//    fast-math compilers that fold `x != x`.
//  - OpImageQueryLod (textureQueryLod()): computed from the coordinate's
//    screen-space derivatives and the image's size and mip count.
//  - The PointCoord built-in: WebGPU points are always one pixel, so it is the
//    pixel center, vec2(0.5). Godot's emulated point sprites never read it.
//  - The EarlyFragmentTests execution mode: WGSL has no way to request it.
//    Without it, depth testing still happens; a fragment that would have been
//    rejected early may additionally run its side effects (texture
//    streaming's material feedback over-reports, it never under-reports).
bool lower_wgsl_unsupported_ops(std::vector<uint32_t> &r_words);

// Turns each array of textures, samplers or storage images
// (`uniform texture3D t[N]`) into N separately bound variables, and every
// dynamically indexed access into a switch over the index whose case k uses
// variable k. Element bindings follow spirv_binding_layout.h. Arrays that do
// not fit that layout, runtime-sized arrays, and arrays used in ways the pass
// does not recognize are left alone for flatten_binding_arrays() to collapse
// to element 0, as it did with every array before this pass existed.
//
// Requires inline_opaque_functions() first: a handle passed to a function
// cannot be switched over at the call site.
bool expand_binding_arrays(std::vector<uint32_t> &r_words);

// Applies the samplers' mipmap LOD bias in the shader. Godot gives the
// samplers of the scene shaders' render-pass set a bias (the viewport's
// texture_mipmap_bias, -0.5 with TAA or FSR2, log2 of the 3D scale with FSR);
// WebGPU samplers cannot carry one, so it was silently dropped. Where a module
// declares the SamplerLodBias uniform buffer (at
// spirv_binding_layout::SAMPLER_LOD_BIAS_GLSL_BINDING), every implicit-LOD
// sample through a sampler of the same set gets its value added as a Bias
// operand. Explicit-LOD and gradient samples are left alone: Vulkan applies a
// sampler's bias to those too, but measured on Godot's renderer it moves
// nothing, and WGSL has no biased form of them. Depth-comparison samples have
// no biased form in WGSL either.
bool apply_sampler_lod_bias(std::vector<uint32_t> &r_words);

// Runs SPIRV-Tools' validator over a module in the target environment every
// pass builds its IR in (SPV_ENV_VULKAN_1_2: SPIR-V 1.0 to 1.5; the driver asks
// glslang for 1.3). On failure, writes the validator's first error to *r_error
// (if given). SPIRV-Tools' optimizer and IR analyses *assert* on malformed
// input instead of reporting it (or, with NDEBUG, read through null pointers),
// so this is the gate spirv_preprocess::run_pipeline() puts in front of them.
// Safe on arbitrary bytes: the validator is the part of SPIRV-Tools written
// for input nobody vouches for.
bool validate_module(const uint32_t *p_words, size_t p_word_count, std::string *r_error = nullptr);

// The structural invariants SPIRV-Tools' IR loader and analyses assert on
// rather than report: a parseable instruction stream, an id bound with room
// for the ids a pass adds, every result id under it and defined once, every id operand defined somewhere in
// the module, OpEntryPoint and OpFunctionCall naming an OpFunction, and every
// basic block ending in a terminator. One linear scan, a fraction of
// validate_module()'s cost, so every pass that builds the optimizer's IR checks
// it first: run_pipeline() validates only its input, and a module one of the
// raw word-rewriting passes left with a dangling reference (an OpName for a
// deleted variable, say) must make the next optimizer pass skip, not abort.
// Not a validator: a module that passes can still be invalid, and the
// optimizer still asserts on some invalid modules this lets through (an OpLoad
// from a non-pointer, in AggressiveDCE). Input nobody vouches for goes through
// validate_module().
bool check_ir_preconditions(const uint32_t *p_words, size_t p_word_count, std::string *r_error = nullptr);

} // namespace spirv_opt_passes
