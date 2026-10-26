/**************************************************************************/
/*  wgsl_text_scan.h                                                      */
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

// Everything the WebGPU driver learns by reading converted WGSL text, in one
// place with no engine types in it (std only), so the same code runs inside the
// driver and inside a native test over both converters' output
// (webgpu_tests/wgsl_text_scan, webgpu_notes/TASKS.md §2.35).
//
// Why a parser and not strstr(): the driver derives bind group layout entries
// (texture dimension and sample type, depth, multisampled, comparison samplers,
// storage-texture format and access, buffer access, override ids) from the text.
// A scan that finds nothing does not fail -- the binding silently gets a default
// (a blank texture, a wrong sample type, an override left unset). Tint wrote
// "@group(0u) @binding(1u) var<storage> x: T;"; SPIRV-Cross writes
// "@group(0) @binding(1) var<storage, read> x : T;". This reads module-scope
// declarations structurally -- any attribute order, any whitespace, optional
// integer suffixes -- so both parse to the same answers.

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace wgsl_text_scan {

// A module-scope `var` carrying @group and @binding.
struct ResourceVar {
	uint32_t group = 0;
	uint32_t binding = 0;
	std::string name;
	std::string address_space; // "uniform", "storage", "" for handles (textures, samplers).
	std::string access; // The second var<> argument ("read", "read_write"), "" if absent.
	std::string type; // Whole type text, trimmed.
	std::string type_name; // Leading identifier: "texture_2d", "sampler", "array", ...
	std::vector<std::string> type_args; // Trimmed top-level generic arguments.
	size_t name_begin = 0;
	size_t type_begin = 0; // [type_begin, type_end) is the type text in the source.
	size_t type_end = 0;
	size_t decl_end = 0; // Offset of the terminating ';'.

	uint32_t key() const { return (group << 16) | binding; }
};

std::vector<ResourceVar> scan_resource_vars(const std::string &p_wgsl);

enum Dim {
	DIM_UNDEFINED,
	DIM_1D,
	DIM_2D,
	DIM_2D_ARRAY,
	DIM_3D,
	DIM_CUBE,
	DIM_CUBE_ARRAY,
};

enum SampleKind {
	SAMPLE_NONE, // Depth and storage textures, non-textures.
	SAMPLE_FLOAT,
	SAMPLE_UINT,
	SAMPLE_SINT,
};

// What the driver's bind group layout builder wants to know about one resource.
struct Description {
	bool is_sampler = false;
	bool is_comparison_sampler = false;
	Dim dim = DIM_UNDEFINED;
	bool is_texture = false;
	bool is_depth = false;
	bool is_multisampled = false;
	bool is_storage_texture = false;
	SampleKind sample_kind = SAMPLE_NONE;
	std::string storage_format; // "rgba8unorm", ...
	std::string storage_access; // "read", "write", "read_write" ("" when unrecognized).
	bool is_uniform = false;
	bool is_storage_buffer = false;
	bool storage_read_only = false; // Only meaningful with is_storage_buffer.
};

Description describe(const ResourceVar &p_var);

// `@id(N) override NAME ...` declarations.
struct OverrideDecl {
	uint32_t id = 0;
	std::string name; // Empty when it could not be read.
	bool followed_by_override = false; // False: `@id(N)` on something else.
	bool name_used = false; // NAME appears outside its own declaration line.
};

std::vector<OverrideDecl> scan_overrides(const std::string &p_wgsl);

// Every `textureSample*(TEX, SAMPLER, ...)` / `textureGather(TEX, SAMPLER, ...)`
// call (not the comparison ones) whose first two arguments are plain identifiers
// naming resource vars: the (texture key, sampler key) pairs. The driver uses them
// to give a sampler used with a depth texture the NonFiltering binding type.
struct TextureSamplerPair {
	uint32_t texture_key = 0;
	uint32_t sampler_key = 0;
};

std::vector<TextureSamplerPair> scan_texture_sampler_pairs(const std::string &p_wgsl, const std::vector<ResourceVar> &p_vars);

// --- Rewrites. All return the number of declarations they changed. ---

struct RWSplit {
	uint32_t group = 0;
	uint32_t binding = 0;
	uint32_t shadow_binding = 0;
	std::string var_name;
	std::string format;
	bool is_new = true; // False when a caller-supplied split was reused.
};

// Splits `texture_storage_*<fmt, read_write>` into a write storage texture plus a
// sampled `<name>_rw_in` shadow at binding+1, for devices without read_write
// storage textures. A format p_keep_read_write(fmt) accepts is left alone.
// p_only (when not null) restricts the split to those bindings and fixes their
// shadow binding (the legacy specialization path must agree with the layout the
// primary module already built).
std::vector<RWSplit> split_read_write_storage(std::string &r_wgsl, const std::function<bool(const std::string &)> &p_keep_read_write, const std::vector<std::pair<uint32_t, uint32_t>> *p_only = nullptr);

struct ReadStorageConversion {
	uint32_t group = 0;
	uint32_t binding = 0;
	std::string format;
};

// Turns `texture_storage_*<fmt, read>` into a sampled `texture_*<f32|u32|i32>`
// and gives its textureLoad calls the mip level argument.
std::vector<ReadStorageConversion> convert_read_storage_to_sampled(std::string &r_wgsl);

// Rewrites single-component-read `texture_2d<f32>` bindings with "depth" in the
// name to texture_depth_2d (see the .cpp). Returns whether anything changed.
bool reclassify_single_component_depth_textures(std::string &r_wgsl, const std::set<uint32_t> &p_exclude_keys);

} // namespace wgsl_text_scan
