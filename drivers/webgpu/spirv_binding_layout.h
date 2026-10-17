/**************************************************************************/
/*  spirv_binding_layout.h                                                */
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

#include <cstdint>

// Where the WGSL bindings of a GLSL binding end up, shared by the SPIR-V
// passes that assign them and the driver that builds bind groups for them.
// Dependency-free so both the web driver and the native tint_convert_cli can
// include it.
//
// Every GLSL binding b becomes WGSL binding 2b (2b + 1 holds the sampler split
// out of a combined image sampler, see split_combined_samplers()).
//
// WGSL has no arrays of textures, samplers or storage textures that Chrome
// accepts, so expand_binding_arrays() (spirv_opt_passes.h) turns a GLSL handle
// array `uniform texture3D t[N]` into N separate variables. Element 0 keeps
// binding 2b; element k > 0 gets a binding of its own from a range above
// every ordinary doubled binding. The mapping depends only on (b, k), so every
// shader that declares the same array agrees on it and a bind group built for
// one is valid for the others.
namespace spirv_binding_layout {

// First WGSL binding of the expanded-element range. Ordinary doubled bindings
// stay below it as long as GLSL bindings stay below 200, and it is clear of the
// push-constant ring buffer's binding 120.
constexpr uint32_t ARRAY_ELEMENT_BINDING_BASE = 400;
// Elements 1..16 of an array; with element 0 that is 17 elements.
constexpr uint32_t ARRAY_ELEMENT_BINDING_STRIDE = 16;
// WebGPU's maxBindingsPerBindGroup default; every binding number must stay below it.
constexpr uint32_t MAX_BINDINGS_PER_BIND_GROUP = 1000;

constexpr uint32_t MAX_EXPANDED_ARRAY_LENGTH = ARRAY_ELEMENT_BINDING_STRIDE + 1;

// WGSL binding of element p_element of the array declared at GLSL binding p_glsl_binding.
constexpr uint32_t array_element_binding(uint32_t p_glsl_binding, uint32_t p_element) {
	return p_element == 0 ? p_glsl_binding * 2 : ARRAY_ELEMENT_BINDING_BASE + p_glsl_binding * ARRAY_ELEMENT_BINDING_STRIDE + (p_element - 1);
}

// Whether an array of p_length elements at GLSL binding p_glsl_binding fits
// the expanded-element range. Arrays that do not are collapsed to element 0
// by flatten_binding_arrays() instead, as every array was before expansion.
constexpr bool array_can_expand(uint32_t p_glsl_binding, uint32_t p_length) {
	return p_length >= 2 && p_length <= MAX_EXPANDED_ARRAY_LENGTH &&
			array_element_binding(p_glsl_binding, p_length - 1) < MAX_BINDINGS_PER_BIND_GROUP;
}

// GLSL binding (in whichever set declares it) of the scene shaders'
// SamplerLodBias uniform buffer, holding the mipmap bias of the samplers in
// that same set. apply_sampler_lod_bias() (spirv_opt_passes.h) applies it,
// since WebGPU samplers have no LOD bias of their own.
constexpr uint32_t SAMPLER_LOD_BIAS_GLSL_BINDING = 40;

} // namespace spirv_binding_layout
