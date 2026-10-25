/**************************************************************************/
/*  wgsl_text_scan.cpp                                                    */
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

#include "wgsl_text_scan.h"

#include <cstring>
#include <map>

namespace wgsl_text_scan {

static bool _is_ident_char(char p_c) {
	return (p_c >= 'a' && p_c <= 'z') || (p_c >= 'A' && p_c <= 'Z') || (p_c >= '0' && p_c <= '9') || p_c == '_';
}

static bool _is_space(char p_c) {
	return p_c == ' ' || p_c == '\t' || p_c == '\n' || p_c == '\r';
}

static size_t _skip_space(const std::string &p_s, size_t p_i) {
	while (p_i < p_s.size() && _is_space(p_s[p_i])) {
		p_i++;
	}
	return p_i;
}

// Skips whitespace and comments.
static size_t _skip_trivia(const std::string &p_s, size_t p_i) {
	while (true) {
		p_i = _skip_space(p_s, p_i);
		if (p_i + 1 < p_s.size() && p_s[p_i] == '/' && p_s[p_i + 1] == '/') {
			while (p_i < p_s.size() && p_s[p_i] != '\n') {
				p_i++;
			}
		} else if (p_i + 1 < p_s.size() && p_s[p_i] == '/' && p_s[p_i + 1] == '*') {
			size_t e = p_s.find("*/", p_i + 2);
			p_i = e == std::string::npos ? p_s.size() : e + 2;
		} else {
			return p_i;
		}
	}
}

static std::string _trim(const std::string &p_s) {
	size_t b = 0, e = p_s.size();
	while (b < e && _is_space(p_s[b])) {
		b++;
	}
	while (e > b && _is_space(p_s[e - 1])) {
		e--;
	}
	return p_s.substr(b, e - b);
}

// Reads an identifier at p_i. Returns its end (p_i when there is none).
static size_t _read_ident(const std::string &p_s, size_t p_i) {
	if (p_i >= p_s.size() || !(_is_ident_char(p_s[p_i])) || (p_s[p_i] >= '0' && p_s[p_i] <= '9')) {
		return p_i;
	}
	while (p_i < p_s.size() && _is_ident_char(p_s[p_i])) {
		p_i++;
	}
	return p_i;
}

// p_open points at an opening bracket; returns the index of its match, or npos.
static size_t _match_bracket(const std::string &p_s, size_t p_open, char p_o, char p_c) {
	int depth = 0;
	for (size_t i = p_open; i < p_s.size(); i++) {
		if (p_s[i] == p_o) {
			depth++;
		} else if (p_s[i] == p_c) {
			depth--;
			if (depth == 0) {
				return i;
			}
		}
	}
	return std::string::npos;
}

// "12", "12u", "12i", " 12 u " -> 12. False when it is not a plain integer literal.
static bool _parse_uint(const std::string &p_text, uint32_t &r_value) {
	std::string t = _trim(p_text);
	if (t.empty()) {
		return false;
	}
	size_t i = 0;
	uint64_t v = 0;
	while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
		v = v * 10 + (uint64_t)(t[i] - '0');
		i++;
	}
	if (i == 0) {
		return false;
	}
	if (i < t.size() && (t[i] == 'u' || t[i] == 'i')) {
		i++;
	}
	if (i != t.size()) {
		return false;
	}
	r_value = (uint32_t)v;
	return true;
}

static std::vector<std::string> _split_top_level(const std::string &p_s) {
	std::vector<std::string> out;
	int depth = 0;
	size_t start = 0;
	for (size_t i = 0; i < p_s.size(); i++) {
		char c = p_s[i];
		if (c == '<' || c == '(' || c == '[') {
			depth++;
		} else if (c == '>' || c == ')' || c == ']') {
			depth--;
		} else if (c == ',' && depth == 0) {
			out.push_back(_trim(p_s.substr(start, i - start)));
			start = i + 1;
		}
	}
	std::string last = _trim(p_s.substr(start));
	if (!last.empty() || !out.empty()) {
		out.push_back(last);
	}
	return out;
}

struct Attribute {
	std::string name;
	std::string args;
	bool has_args = false;
};

// Reads `@name` or `@name(args)` at p_i (which points at '@'). Returns the end, or p_i on failure.
static size_t _read_attribute(const std::string &p_s, size_t p_i, Attribute &r_attr) {
	size_t i = p_i + 1;
	i = _skip_space(p_s, i);
	size_t e = _read_ident(p_s, i);
	if (e == i) {
		return p_i;
	}
	r_attr = Attribute();
	r_attr.name = p_s.substr(i, e - i);
	size_t j = _skip_space(p_s, e);
	if (j < p_s.size() && p_s[j] == '(') {
		size_t close = _match_bracket(p_s, j, '(', ')');
		if (close == std::string::npos) {
			return p_i;
		}
		r_attr.args = p_s.substr(j + 1, close - j - 1);
		r_attr.has_args = true;
		return close + 1;
	}
	return e;
}

struct TypeText {
	std::string name;
	std::vector<std::string> args;
};

static TypeText _parse_type(const std::string &p_type) {
	TypeText t;
	size_t e = _read_ident(p_type, 0);
	t.name = p_type.substr(0, e);
	size_t j = _skip_space(p_type, e);
	if (j < p_type.size() && p_type[j] == '<') {
		size_t close = _match_bracket(p_type, j, '<', '>');
		if (close != std::string::npos) {
			t.args = _split_top_level(p_type.substr(j + 1, close - j - 1));
		}
	}
	return t;
}

// Walks module-scope declarations that start with attributes. Calls p_on_var for
// `var`, p_on_override for `override`. The attributes are read in any order.
struct DeclVisitor {
	std::function<void(const std::vector<Attribute> &, size_t p_kw_end, size_t p_attr_begin)> on_var;
	std::function<void(const std::vector<Attribute> &, size_t p_kw_end, size_t p_attr_begin)> on_override;
};

static void _walk_attributed_decls(const std::string &p_s, const DeclVisitor &p_v) {
	size_t i = 0;
	while (i < p_s.size()) {
		char c = p_s[i];
		if (c == '/' && i + 1 < p_s.size() && (p_s[i + 1] == '/' || p_s[i + 1] == '*')) {
			i = _skip_trivia(p_s, i);
			continue;
		}
		if (c != '@') {
			i++;
			continue;
		}
		size_t attr_begin = i;
		std::vector<Attribute> attrs;
		while (i < p_s.size() && p_s[i] == '@') {
			Attribute a;
			size_t e = _read_attribute(p_s, i, a);
			if (e == i) {
				break;
			}
			attrs.push_back(a);
			i = _skip_trivia(p_s, e);
		}
		size_t kw_end = _read_ident(p_s, i);
		if (kw_end == i) {
			i++;
			continue;
		}
		std::string kw = p_s.substr(i, kw_end - i);
		if (kw == "var" && p_v.on_var) {
			p_v.on_var(attrs, kw_end, attr_begin);
		} else if (kw == "override" && p_v.on_override) {
			p_v.on_override(attrs, kw_end, attr_begin);
		}
		i = kw_end;
	}
}

std::vector<ResourceVar> scan_resource_vars(const std::string &p_wgsl) {
	std::vector<ResourceVar> out;
	DeclVisitor v;
	v.on_var = [&](const std::vector<Attribute> &p_attrs, size_t p_kw_end, size_t) {
		bool has_group = false, has_binding = false;
		ResourceVar r;
		for (const Attribute &a : p_attrs) {
			if (a.name == "group" && _parse_uint(a.args, r.group)) {
				has_group = true;
			} else if (a.name == "binding" && _parse_uint(a.args, r.binding)) {
				has_binding = true;
			}
		}
		if (!has_group || !has_binding) {
			return;
		}
		size_t i = _skip_trivia(p_wgsl, p_kw_end);
		if (i < p_wgsl.size() && p_wgsl[i] == '<') {
			size_t close = _match_bracket(p_wgsl, i, '<', '>');
			if (close == std::string::npos) {
				return;
			}
			std::vector<std::string> sp = _split_top_level(p_wgsl.substr(i + 1, close - i - 1));
			if (sp.size() > 0) {
				r.address_space = sp[0];
			}
			if (sp.size() > 1) {
				r.access = sp[1];
			}
			i = _skip_trivia(p_wgsl, close + 1);
		}
		size_t ne = _read_ident(p_wgsl, i);
		if (ne == i) {
			return;
		}
		r.name = p_wgsl.substr(i, ne - i);
		r.name_begin = i;
		i = _skip_trivia(p_wgsl, ne);
		if (i >= p_wgsl.size() || p_wgsl[i] != ':') {
			return;
		}
		i = _skip_trivia(p_wgsl, i + 1);
		size_t semi = p_wgsl.find(';', i);
		if (semi == std::string::npos) {
			return;
		}
		size_t te = semi;
		while (te > i && _is_space(p_wgsl[te - 1])) {
			te--;
		}
		r.type_begin = i;
		r.type_end = te;
		r.decl_end = semi;
		r.type = p_wgsl.substr(i, te - i);
		TypeText t = _parse_type(r.type);
		r.type_name = t.name;
		r.type_args = t.args;
		out.push_back(r);
	};
	_walk_attributed_decls(p_wgsl, v);
	return out;
}

Description describe(const ResourceVar &p_var) {
	Description d;
	const std::string &n = p_var.type_name;
	if (n == "sampler_comparison") {
		d.is_sampler = true;
		d.is_comparison_sampler = true;
	} else if (n == "sampler") {
		d.is_sampler = true;
	}
	if (n.compare(0, 8, "texture_") == 0) {
		d.is_texture = true;
		d.is_storage_texture = n.compare(0, 16, "texture_storage_") == 0;
		d.is_depth = n.compare(0, 14, "texture_depth_") == 0;
		if (n == "texture_depth_multisampled_2d") {
			d.dim = DIM_2D;
			d.is_multisampled = true;
		} else if (n == "texture_multisampled_2d") {
			d.dim = DIM_2D;
			d.is_multisampled = true;
		} else if (n == "texture_depth_2d_array") {
			d.dim = DIM_2D_ARRAY;
		} else if (n == "texture_depth_cube_array") {
			d.dim = DIM_CUBE_ARRAY;
		} else if (n == "texture_depth_cube") {
			d.dim = DIM_CUBE;
		} else if (n == "texture_depth_2d") {
			d.dim = DIM_2D;
		} else if (n == "texture_2d_array") {
			d.dim = DIM_2D_ARRAY;
		} else if (n == "texture_cube_array") {
			d.dim = DIM_CUBE_ARRAY;
		} else if (n == "texture_cube") {
			d.dim = DIM_CUBE;
		} else if (n == "texture_3d") {
			d.dim = DIM_3D;
		} else if (n == "texture_2d") {
			d.dim = DIM_2D;
		} else if (n == "texture_1d") {
			d.dim = DIM_1D;
		} else if (n == "texture_storage_2d_array") {
			d.dim = DIM_2D_ARRAY;
		} else if (n == "texture_storage_cube_array") {
			d.dim = DIM_CUBE_ARRAY;
		} else if (n == "texture_storage_2d") {
			d.dim = DIM_2D;
		} else if (n == "texture_storage_3d") {
			d.dim = DIM_3D;
		}
		if (d.is_storage_texture) {
			if (p_var.type_args.size() > 0) {
				d.storage_format = p_var.type_args[0];
			}
			if (p_var.type_args.size() > 1) {
				const std::string &a = p_var.type_args[1];
				if (a == "read_write" || a == "write" || a == "read") {
					d.storage_access = a;
				}
			}
		} else if (!d.is_depth && !p_var.type_args.empty()) {
			const std::string &c = p_var.type_args[0];
			d.sample_kind = c == "u32" ? SAMPLE_UINT : (c == "i32" ? SAMPLE_SINT : SAMPLE_FLOAT);
		}
	}
	if (p_var.address_space == "uniform") {
		d.is_uniform = true;
	} else if (p_var.address_space == "storage") {
		d.is_storage_buffer = true;
		d.storage_read_only = p_var.access != "read_write" && p_var.access != "write";
	}
	return d;
}

std::vector<OverrideDecl> scan_overrides(const std::string &p_wgsl) {
	std::vector<OverrideDecl> out;
	// `@id(N)` on anything. An `@id` that is not followed by `override` is still
	// reported (the driver includes those to be safe).
	size_t i = 0;
	while (i < p_wgsl.size()) {
		if (p_wgsl[i] == '/' && i + 1 < p_wgsl.size() && (p_wgsl[i + 1] == '/' || p_wgsl[i + 1] == '*')) {
			i = _skip_trivia(p_wgsl, i);
			continue;
		}
		if (p_wgsl[i] != '@') {
			i++;
			continue;
		}
		std::vector<Attribute> attrs;
		while (i < p_wgsl.size() && p_wgsl[i] == '@') {
			Attribute a;
			size_t e = _read_attribute(p_wgsl, i, a);
			if (e == i) {
				break;
			}
			attrs.push_back(a);
			i = _skip_trivia(p_wgsl, e);
		}
		bool has_id = false;
		OverrideDecl o;
		for (const Attribute &a : attrs) {
			if (a.name == "id" && _parse_uint(a.args, o.id)) {
				has_id = true;
			}
		}
		if (!has_id) {
			if (attrs.empty()) {
				i++;
			}
			continue;
		}
		size_t kw_end = _read_ident(p_wgsl, i);
		if (kw_end > i && p_wgsl.compare(i, kw_end - i, "override") == 0) {
			o.followed_by_override = true;
			size_t ns = _skip_trivia(p_wgsl, kw_end);
			size_t ne = _read_ident(p_wgsl, ns);
			if (ne > ns && ne - ns < 256) {
				o.name = p_wgsl.substr(ns, ne - ns);
				// Usage beyond the declaration's own line.
				size_t line_end = p_wgsl.find('\n', ne);
				size_t search = line_end == std::string::npos ? p_wgsl.size() : line_end;
				while ((search = p_wgsl.find(o.name, search)) != std::string::npos) {
					bool before = search == 0 || !_is_ident_char(p_wgsl[search - 1]);
					size_t after_i = search + o.name.size();
					bool after = after_i >= p_wgsl.size() || !_is_ident_char(p_wgsl[after_i]);
					if (before && after) {
						o.name_used = true;
						break;
					}
					search++;
				}
			}
			i = kw_end;
		}
		out.push_back(o);
	}
	return out;
}

// Index of the ')' matching the '(' at p_open_paren, or -1 if unbalanced.
static int64_t _find_matching_paren(const char *p_str, int64_t p_len, int64_t p_open_paren) {
	int depth = 0;
	for (int64_t i = p_open_paren; i < p_len; i++) {
		if (p_str[i] == '(') {
			depth++;
		} else if (p_str[i] == ')') {
			depth--;
			if (depth == 0) {
				return i;
			}
		}
	}
	return -1;
}

// Finds the [start, end) byte range of the p_index'th (0-based) top-level
// comma-separated argument within [p_args_start, p_args_end) (the region
// strictly inside a call's parens). Sets r_start to -1 if there aren't
// enough top-level arguments.
static void _find_nth_top_level_arg(const char *p_str, int64_t p_args_start, int64_t p_args_end, int p_index, int64_t &r_start, int64_t &r_end) {
	int depth = 0;
	int64_t arg_start = p_args_start;
	int arg_idx = 0;
	for (int64_t i = p_args_start; i < p_args_end; i++) {
		char c = p_str[i];
		if (c == '(' || c == '[') {
			depth++;
		} else if (c == ')' || c == ']') {
			depth--;
		} else if (c == ',' && depth == 0) {
			if (arg_idx == p_index) {
				r_start = arg_start;
				r_end = i;
				return;
			}
			arg_idx++;
			arg_start = i + 1;
		}
	}
	if (arg_idx == p_index) {
		r_start = arg_start;
		r_end = p_args_end;
		return;
	}
	r_start = -1;
	r_end = -1;
}

static bool _lookup_binding(const std::map<size_t, uint32_t> &p_name_keys, int64_t p_name_start, unsigned int &r_grp, unsigned int &r_bnd) {
	std::map<size_t, uint32_t>::const_iterator it = p_name_keys.find((size_t)p_name_start);
	if (it == p_name_keys.end()) {
		return false;
	}
	r_grp = it->second >> 16;
	r_bnd = it->second & 0xFFFF;
	return true;
}

// A call result bound to a SPIRV-Cross id (`let _27 = <call>;`) whose every other use
// in the module is `_27.x` / `_27.r`: the edits that drop each swizzle, as [start, end)
// ranges. The id form (`_` and digits) is unique module-wide in SPIRV-Cross's output, which is
// what makes "every use of this name" the uses of this one value.
struct _SwizzleCut {
	int64_t start, end;
};

static bool _forwarded_uses(const char *p_wgsl, int64_t p_len, int64_t p_call_start, int64_t p_after_call, std::vector<_SwizzleCut> &r_cuts) {
	int64_t i = p_after_call;
	while (i < p_len && (p_wgsl[i] == ' ' || p_wgsl[i] == '\t')) {
		i++;
	}
	if (i >= p_len || p_wgsl[i] != ';') {
		return false;
	}
	int64_t j = p_call_start - 1;
	while (j >= 0 && (p_wgsl[j] == ' ' || p_wgsl[j] == '\t')) {
		j--;
	}
	if (j < 0 || p_wgsl[j] != '=') {
		return false;
	}
	j--;
	while (j >= 0 && (p_wgsl[j] == ' ' || p_wgsl[j] == '\t')) {
		j--;
	}
	int64_t name_end = j + 1;
	while (j >= 0 && _is_ident_char(p_wgsl[j])) {
		j--;
	}
	int64_t name_start = j + 1;
	if (name_end - name_start < 2 || p_wgsl[name_start] != '_') {
		return false;
	}
	for (int64_t k = name_start + 1; k < name_end; k++) {
		if (p_wgsl[k] < '0' || p_wgsl[k] > '9') {
			return false;
		}
	}
	while (j >= 0 && (p_wgsl[j] == ' ' || p_wgsl[j] == '\t')) {
		j--;
	}
	if (j < 2 || strncmp(p_wgsl + j - 2, "let", 3) != 0 || (j >= 3 && _is_ident_char(p_wgsl[j - 3]))) {
		return false;
	}
	const std::string name(p_wgsl + name_start, (size_t)(name_end - name_start));
	size_t pos = 0;
	const std::string all(p_wgsl, (size_t)p_len);
	bool any = false;
	while ((pos = all.find(name, pos)) != std::string::npos) {
		int64_t at = (int64_t)pos;
		pos += name.size();
		if (at == name_start) {
			continue;
		}
		if ((at > 0 && (_is_ident_char(p_wgsl[at - 1]) || p_wgsl[at - 1] == '.')) || (at + (int64_t)name.size() < p_len && _is_ident_char(p_wgsl[at + name.size()]))) {
			continue; // A different identifier that merely contains this one.
		}
		int64_t after = at + (int64_t)name.size();
		if (after + 1 < p_len && p_wgsl[after] == '.' && (p_wgsl[after + 1] == 'x' || p_wgsl[after + 1] == 'r') &&
				(after + 2 >= p_len || !_is_ident_char(p_wgsl[after + 2]))) {
			r_cuts.push_back({ after, after + 2 });
			any = true;
		} else {
			return false;
		}
	}
	return any;
}

// Detects texture_2d<f32> / texture_multisampled_2d<f32> / texture_cube<f32>
// variables that are only ever read through a single-component swizzle (.x/.r)
// after a plain sample/load, and whose name contains "depth", and rewrites the
// declaration and every such call to the matching texture_depth_* type (dropping
// the swizzle; textureGather loses its component argument; textureSampleLevel's
// level becomes i32). Rationale and the false-positive exclusions (storage
// conversions, "half", "dilated", the scene shaders' group-1 depth_buffer) are
// the history of webgpu_notes/TASKS.md Task 7.13, 7.22, 9.5 and 44; they moved
// here unchanged from rendering_device_driver_webgpu.cpp.
bool reclassify_single_component_depth_textures(std::string &r_wgsl, const std::set<uint32_t> &p_storage_converted_keys) {
	static const char *const TEX_TYPE = "texture_2d<f32>";
	static const int TEX_TYPE_LEN = 15;
	// Multisampled depth-format combined samplers (GLSL sampler2DMS bound to a real
	// depth attachment, e.g. resolve.glsl's MODE_RESOLVE_DEPTH/MODE_RESOLVE_GI
	// `source_depth`, read via texelFetch(...).r -- the same structural/naming
	// shape this whole pass targets) were falling through this scan entirely: Tint's
	// natural non-depth output for a multisampled texture is
	// `texture_multisampled_2d<f32>`, not `texture_2d<f32>`, so the single-string
	// search above never matched them, and they never got reclassified to
	// `texture_depth_multisampled_2d` (WGSL's multisampled depth type -- no `<f32>`
	// parameter, unlike its non-depth counterpart). Left un-reclassified, the real
	// bound resource (a genuine multisampled depth texture, e.g. Forward+'s MSAA
	// depth buffer) mismatched the BGL's Float-expecting entry, and this driver's
	// *existing* depth/float-mismatch fallback (see uniform_set_create()'s
	// `fallback_ms_texture` substitution) silently kicked in -- avoiding a crash,
	// but replacing real MSAA depth data with an all-zero texture, corrupting
	// anything that consumes the result (SDFGI's GI resolve, DOF, ...) without a
	// single error printed. See webgpu_notes/TASKS.md Task 24 round 3.
	static const char *const TEX_TYPE_MS = "texture_multisampled_2d<f32>";
	static const int TEX_TYPE_MS_LEN = 28;
	// Depth cubemaps sampled for their value: cube_to_dp.glsl's
	// `source_depth_cube`, which copies an omni light's cube shadow into the
	// shadow atlas. Left as texture_cube<f32>, the real depth cubemap could not
	// be bound and the blank fallback cube was sampled instead, so every omni
	// light in Cube shadow mode -- the default -- cast no shadow at all.
	static const char *const TEX_TYPE_CUBE = "texture_cube<f32>";
	static const int TEX_TYPE_CUBE_LEN = 17;
	// textureSampleBias/textureSampleGrad are NOT defined for texture_depth_2d
	// in WGSL at all (depth textures don't support mip bias/gradient
	// sampling) -- deliberately excluded here so any texture used via either
	// disqualifies (falls through to "no matching call prefix" below).
	static const char *const CALL_FNS[] = {
		"textureSampleLevel(",
		"textureSample(",
		"textureLoad(",
	};
	static const int CALL_FN_COUNT = 3;

	const char *wgsl = r_wgsl.c_str();
	const int64_t len = (int64_t)strlen(wgsl);
	std::map<size_t, uint32_t> name_keys;
	for (const ResourceVar &rv : scan_resource_vars(r_wgsl)) {
		name_keys[rv.name_begin] = rv.key();
	}

	enum CandidateKind {
		CANDIDATE_2D,
		CANDIDATE_MULTISAMPLED,
		CANDIDATE_CUBE,
	};
	struct Candidate {
		std::string name;
		int64_t name_start = 0;
		int64_t type_start = 0;
		int type_len = 0;
		CandidateKind kind = CANDIDATE_2D;
	};
	std::vector<Candidate> candidates;
	for (int pass = 0; pass < 3; pass++) {
		const char *tex_type = pass == 0 ? TEX_TYPE : (pass == 1 ? TEX_TYPE_MS : TEX_TYPE_CUBE);
		int tex_type_len = pass == 0 ? TEX_TYPE_LEN : (pass == 1 ? TEX_TYPE_MS_LEN : TEX_TYPE_CUBE_LEN);
		CandidateKind kind = (CandidateKind)pass;
		const char *scan = wgsl;
		while (true) {
			const char *found = strstr(scan, tex_type);
			if (!found) {
				break;
			}
			int64_t type_pos = found - wgsl;
			int64_t i = type_pos - 1;
			while (i >= 0 && (wgsl[i] == ' ' || wgsl[i] == '\t')) {
				i--;
			}
			if (i >= 0 && wgsl[i] == ':') {
				i--;
				while (i >= 0 && (wgsl[i] == ' ' || wgsl[i] == '\t')) {
					i--;
				}
				int64_t name_end = i + 1;
				while (i >= 0 && _is_ident_char(wgsl[i])) {
					i--;
				}
				int64_t name_start = i + 1;
				if (name_start < name_end) {
					Candidate c;
					c.name = std::string(wgsl + name_start, (size_t)(name_end - name_start));
					c.name_start = name_start;
					c.type_start = type_pos;
					c.type_len = tex_type_len;
					c.kind = kind;
					candidates.push_back(c);
				}
			}
			scan = found + tex_type_len;
		}
	}
	if (candidates.empty()) {
		return false;
	}

	struct Edit {
		int64_t start = 0;
		int64_t end = 0; // [start, end) in the ORIGINAL text.
		std::string replacement;
	};
	std::vector<Edit> edits;
	auto _forwarded_edits = [&](const char *p_wgsl, int64_t p_len, int64_t p_call_start, int64_t p_after_call, std::vector<Edit> &r_out) {
		std::vector<_SwizzleCut> cuts;
		if (!_forwarded_uses(p_wgsl, p_len, p_call_start, p_after_call, cuts)) {
			return false;
		}
		for (const _SwizzleCut &cut : cuts) {
			Edit e;
			e.start = cut.start;
			e.end = cut.end;
			r_out.push_back(e);
		}
		return true;
	};

	for (const Candidate &c : candidates) {
		const char *name = c.name.c_str();
		const int name_len = (int)c.name.size();
		if (name_len == 0) {
			continue;
		}

		if (!p_storage_converted_keys.empty()) {
			unsigned int grp = 0, bnd = 0;
			if (_lookup_binding(name_keys, c.name_start, grp, bnd)) {
				uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
				if (p_storage_converted_keys.count(key) != 0) {
					continue;
				}
			}
		}

		// The single-component-access structural heuristic alone is NOT
		// sufficient: plenty of legitimate texture_2d<f32> bindings are
		// single-channel by nature without being depth-format data (e.g.
		// CanvasSdfShaderRD's SDF distance texture, sampled and used the
		// exact same .x-only way). Require the WGSL variable name to also
		// contain "depth" (case-insensitive) -- Godot's own GLSL source
		// consistently names depth-buffer-as-texture uniforms this way
		// (source_depth, depth_tex, depth_buffer, ...) -- as a second,
		// independent signal before touching a binding. This is
		// conservative by design: shaders whose depth-reading uniform
		// happens not to follow this naming convention will keep hitting
		// the pre-existing dummy-texture fallback instead of being fixed,
		// which is no worse than the status quo, whereas a false positive
		// here breaks a previously-working, unrelated shader outright.
		{
			bool has_depth_in_name = false;
			for (int ni = 0; ni + 5 <= name_len; ni++) {
				if ((name[ni] == 'd' || name[ni] == 'D') &&
						(name[ni + 1] == 'e' || name[ni + 1] == 'E') &&
						(name[ni + 2] == 'p' || name[ni + 2] == 'P') &&
						(name[ni + 3] == 't' || name[ni + 3] == 'T') &&
						(name[ni + 4] == 'h' || name[ni + 4] == 'H')) {
					has_depth_in_name = true;
					break;
				}
			}
			if (!has_depth_in_name) {
				continue;
			}

			// Godot's own SSR code (screen_space_reflection_resolve.glsl's
			// "source_depth_half", bound from RB_HIZ -- a plain R32Float linear-
			// depth derivative, never a real depth-attachment texture, see
			// ss_effects.cpp's ssr_resolve()) uses "_half" specifically to
			// distinguish a downsampled/derived depth buffer from the real one
			// ("source_depth", bound from get_depth_texture(), on the very same
			// shader). The name+single-component heuristic alone can't tell
			// these apart -- both are named with "depth" and read single-
			// component -- so treat "half" anywhere in the name as a third,
			// independent disqualifying signal, mirroring how the storage-
			// converted-keys exclusion set already handles the other two known
			// synthetic-binding false-positive sources. A real depth-format
			// binding legitimately named with "half" in it would be highly
			// unusual and is not known to exist in this engine's shaders.
			bool has_half_in_name = false;
			for (int ni = 0; ni + 4 <= name_len; ni++) {
				if ((name[ni] == 'h' || name[ni] == 'H') &&
						(name[ni + 1] == 'a' || name[ni + 1] == 'A') &&
						(name[ni + 2] == 'l' || name[ni + 2] == 'L') &&
						(name[ni + 3] == 'f' || name[ni + 3] == 'F')) {
					has_half_in_name = true;
					break;
				}
			}
			if (has_half_in_name) {
				continue;
			}

			// AMD FidelityFX FSR2's `r_dilatedDepth` (ffx_fsr2_callbacks_glsl.h,
			// read via `texelFetch(r_dilatedDepth, ...).r` -- single-component,
			// exactly this heuristic's structural trigger) is the same false-
			// positive shape again: an ordinary r16f-format buffer FSR2 itself
			// computes and writes (`rw_dilatedDepth`, StoreDilatedDepth), never a
			// real depth-attachment texture, that merely has "Depth" in its name
			// because it holds a dilated *copy* of depth values. Every "dilated"-
			// prefixed FFX resource (DilatedDepth/DilatedMotionVectors/
			// DilatedReactiveMasks) is one of these synthetic intermediate
			// buffers by construction, never the real hardware depth buffer, so
			// "dilated" anywhere in the name is a fourth independent
			// disqualifying signal, mirroring "half" above. Found live: Dawn
			// rejected FSR2's depth-clip pass bind group with "None of the
			// supported sample types (Float|UnfilterableFloat) ... match the
			// expected sample types (Depth)" the first time FSR2 actually ran on
			// WebGPU. See webgpu_notes/TASKS.md's FSR2 task.
			bool has_dilated_in_name = false;
			for (int ni = 0; ni + 7 <= name_len; ni++) {
				if ((name[ni] == 'd' || name[ni] == 'D') &&
						(name[ni + 1] == 'i' || name[ni + 1] == 'I') &&
						(name[ni + 2] == 'l' || name[ni + 2] == 'L') &&
						(name[ni + 3] == 'a' || name[ni + 3] == 'A') &&
						(name[ni + 4] == 't' || name[ni + 4] == 'T') &&
						(name[ni + 5] == 'e' || name[ni + 5] == 'E') &&
						(name[ni + 6] == 'd' || name[ni + 6] == 'D')) {
					has_dilated_in_name = true;
					break;
				}
			}
			if (has_dilated_in_name) {
				continue;
			}

			// The scene shaders' own `depth_buffer` is the fifth false-positive
			// shape, and the one that broke the three 3D demo scenes (Task 44):
			// a *copy* of depth in a colour format, not a depth attachment.
			// `scene_forward_clustered_inc.glsl:440` / `scene_forward_mobile_inc.glsl:397`
			// declare `layout(set = 1, binding = 24/9) uniform texture2D depth_buffer`,
			// which user material code reads through Godot's `DEPTH_TEXTURE`
			// (proximity fade and refraction, `scene/resources/material.cpp:1805`/`:1834`,
			// both `textureLod(depth_texture, ..., 0.0).r`) -- single-component, named
			// "depth", so structurally indistinguishable from Bokeh DOF's genuinely
			// depth-format `source_depth` that this pass exists for. What the engine
			// binds there is `RB_TEX_BACK_DEPTH` (`renderer_scene_render_rd.cpp:418`),
			// created `R32_SFLOAT` with `COLOR_ATTACHMENT_BIT | STORAGE_BIT` because a
			// copy pass renders into it -- upstream's own comment says "Set this as
			// color attachment because we're copying data into it, it's not actually
			// used as a depth buffer". Reclassifying it makes the BGL entry
			// `sampleType: Depth` while a colour `R32Float` is bound, which Dawn
			// rejects outright:
			//   None of the supported sample types (Float|UnfilterableFloat) of
			//   [Texture 1152x648 R32Float] match the expected sample types (Depth).
			// The binding is polymorphic on top of that (`render_forward_clustered.cpp`
			// binds `DEFAULT_RD_TEXTURE_DEPTH` when the copy does not exist), so no
			// single sample type can serve both -- and `Float` is the one that serves
			// the case that actually renders. Left un-reclassified the WGSL keeps
			// `texture_2d<f32>` plus the `.x` swizzle, which is exactly right for the
			// R32Float copy; the fallback path only engages in the rarer
			// default-texture case, where there is no depth copy to read anyway.
			//
			// Gated on group 1 -- the scene shaders' render-buffers set, whose binding
			// names are fixed by engine GLSL -- so the two *other* `depth_buffer`
			// bindings in this engine are untouched: `taa_resolve.glsl:47` (set 0,
			// genuinely fed the real depth texture by `taa.cpp`, and audited in
			// Task 7.13 as a binding this pass *should* rewrite) and
			// `cluster_debug.glsl:65` (set 0). A user material uniform that happened
			// to be named `depth_buffer` lands in group 3, also untouched.
			if (name_len == 12 && strncmp(name, "depth_buffer", 12) == 0) {
				unsigned int grp = 0, bnd = 0;
				if (_lookup_binding(name_keys, c.name_start, grp, bnd) && grp == 1) {
					continue;
				}
			}
		}

		bool disqualified = false;
		std::vector<Edit> call_edits;

		const char *p = wgsl;
		while (true) {
			const char *found = strstr(p, name);
			if (!found) {
				break;
			}
			int64_t pos = found - wgsl;
			p = found + 1;

			bool left_ok = (pos == 0) || !_is_ident_char(wgsl[pos - 1]);
			int64_t after = pos + name_len;
			bool right_ok = (after >= len) || !_is_ident_char(wgsl[after]);
			if (!left_ok || !right_ok) {
				continue;
			}

			if (pos == c.name_start) {
				continue; // The declaration's own identifier -- fine.
			}

			bool matched_call = false;
			for (int fi = 0; fi < CALL_FN_COUNT; fi++) {
				const char *fn = CALL_FNS[fi];
				size_t fn_len = strlen(fn);
				int64_t probe = pos;
				while (probe > 0 && (wgsl[probe - 1] == ' ' || wgsl[probe - 1] == '\t')) {
					probe--;
				}
				int64_t fn_start = probe - (int64_t)fn_len;
				if (fn_start < 0 || strncmp(wgsl + fn_start, fn, fn_len) != 0) {
					continue;
				}
				int64_t open_paren = fn_start + (int64_t)fn_len - 1;
				int64_t close_paren = _find_matching_paren(wgsl, len, open_paren);
				if (close_paren < 0) {
					continue;
				}
				int64_t after_call = close_paren + 1;
				bool direct_swizzle = after_call + 1 < len && wgsl[after_call] == '.' &&
						(wgsl[after_call + 1] == 'x' || wgsl[after_call + 1] == 'r');
				// SPIRV-Cross keeps the whole vec4 in a `let _27 = textureLoad(...);` and
				// swizzles at each use (`_27.x`), where Tint put the swizzle on the call.
				std::vector<Edit> forwarded_edits;
				bool forwarded = !direct_swizzle && _forwarded_edits(wgsl, len, fn_start, after_call, forwarded_edits);
				if (direct_swizzle || forwarded) {
					int64_t after_swizzle = after_call + 2;
					bool swizzle_ok = forwarded || (after_swizzle >= len) || !_is_ident_char(wgsl[after_swizzle]);
					if (swizzle_ok) {
						// texture_depth_2d's textureSampleLevel takes an i32
						// level (unlike texture_2d<f32>'s f32 level) -- wrap
						// the 4th argument (0-indexed: 3) in i32(...) so a
						// float-literal/expression level still type-checks.
						bool level_wrap_ok = true;
						Edit level_edit;
						if (strcmp(fn, "textureSampleLevel(") == 0) {
							int64_t a_start, a_end;
							_find_nth_top_level_arg(wgsl, open_paren + 1, close_paren, 3, a_start, a_end);
							if (a_start < 0) {
								level_wrap_ok = false;
							} else {
								int64_t trimmed_start = a_start;
								int64_t trimmed_end = a_end;
								while (trimmed_start < trimmed_end && (wgsl[trimmed_start] == ' ' || wgsl[trimmed_start] == '\t')) {
									trimmed_start++;
								}
								while (trimmed_end > trimmed_start && (wgsl[trimmed_end - 1] == ' ' || wgsl[trimmed_end - 1] == '\t')) {
									trimmed_end--;
								}
								level_edit.start = a_start;
								level_edit.end = a_end;
								level_edit.replacement = "i32(" + std::string(wgsl + trimmed_start, (size_t)(trimmed_end - trimmed_start)) + ")";
							}
						}
						if (level_wrap_ok) {
							if (direct_swizzle) {
								Edit e;
								e.start = after_call;
								e.end = after_call + 2;
								e.replacement = "";
								call_edits.push_back(e);
							}
							for (const Edit &fe : forwarded_edits) {
								call_edits.push_back(fe);
							}
							if (strcmp(fn, "textureSampleLevel(") == 0) {
								call_edits.push_back(level_edit);
							}
							matched_call = true;
							break;
						}
					}
				}
				break; // Function-prefix matched but a disqualifying usage shape.
			}
			if (!matched_call) {
				// textureDimensions(name, ...) means the same on a depth texture,
				// so it neither disqualifies nor needs rewriting. Shaders that
				// filter a depth texture by hand (gather plus weights) read its
				// size this way -- volumetric_fog_process.glsl's
				// texture_linear_depth().
				static const char *const DIMS_FN = "textureDimensions(";
				static const int64_t DIMS_FN_LEN = 18;
				int64_t probe = pos;
				while (probe > 0 && (wgsl[probe - 1] == ' ' || wgsl[probe - 1] == '\t')) {
					probe--;
				}
				if (probe >= DIMS_FN_LEN && strncmp(wgsl + probe - DIMS_FN_LEN, DIMS_FN, DIMS_FN_LEN) == 0) {
					matched_call = true;
				}
			}
			if (!matched_call) {
				// texture_depth_2d's textureGather overload drops the leading
				// component argument entirely (textureGather(tex, samp, coords)),
				// unlike texture_2d<f32>'s (textureGather(component, tex, samp,
				// coords)) -- GLSL's implicit-default-component textureGather(
				// sampler, uv) always converts to WGSL's explicit `textureGather(
				// 0i, tex, samp, uv)` form, so this isn't a swizzle-drop like the
				// CALL_FNS cases above but an argument-drop. See Task 7.22.
				static const char *const GATHER_FN = "textureGather(";
				static const int64_t GATHER_FN_LEN = 14;
				const char *gscan = wgsl;
				while (true) {
					const char *gfound = strstr(gscan, GATHER_FN);
					if (!gfound) {
						break;
					}
					int64_t g_open = (gfound - wgsl) + GATHER_FN_LEN - 1;
					int64_t g_close = _find_matching_paren(wgsl, len, g_open);
					gscan = gfound + 1;
					if (g_close < 0 || g_open >= pos || g_close <= pos) {
						continue; // This occurrence of `name` isn't inside this call's parens.
					}
					int64_t a1_start, a1_end;
					_find_nth_top_level_arg(wgsl, g_open + 1, g_close, 1, a1_start, a1_end);
					if (a1_start < 0) {
						continue;
					}
					int64_t t_start = a1_start, t_end = a1_end;
					while (t_start < t_end && (wgsl[t_start] == ' ' || wgsl[t_start] == '\t')) {
						t_start++;
					}
					while (t_end > t_start && (wgsl[t_end - 1] == ' ' || wgsl[t_end - 1] == '\t')) {
						t_end--;
					}
					if (t_start != pos || t_end != pos + name_len) {
						continue; // `name` isn't (whitespace aside) exactly the 2nd argument.
					}
					int64_t a0_start, a0_end;
					_find_nth_top_level_arg(wgsl, g_open + 1, g_close, 0, a0_start, a0_end);
					if (a0_start < 0) {
						continue;
					}
					Edit e;
					e.start = a0_start;
					e.end = t_start; // Drop the component arg plus its trailing comma/whitespace.
					e.replacement = "";
					call_edits.push_back(e);
					matched_call = true;
					break;
				}
			}
			if (!matched_call) {
				disqualified = true;
				break;
			}
		}

		if (disqualified || call_edits.empty()) {
			continue;
		}

		Edit decl_edit;
		decl_edit.start = c.type_start;
		decl_edit.end = c.type_start + c.type_len;
		decl_edit.replacement = c.kind == CANDIDATE_MULTISAMPLED ? "texture_depth_multisampled_2d" : (c.kind == CANDIDATE_CUBE ? "texture_depth_cube" : "texture_depth_2d");
		edits.push_back(decl_edit);
		for (const Edit &ce : call_edits) {
			edits.push_back(ce);
		}
	}

	if (edits.empty()) {
		return false;
	}

	// Small insertion sort by start offset (edit counts per shader are tiny).
	for (int i = 1; i < (int)edits.size(); i++) {
		Edit key = edits[i];
		int j = i - 1;
		while (j >= 0 && edits[j].start > key.start) {
			edits[j + 1] = edits[j];
			j--;
		}
		edits[j + 1] = key;
	}

	std::string out;
	int64_t cursor = 0;
	for (const Edit &e : edits) {
		if (e.start < cursor) {
			continue; // Overlap safety net; shouldn't happen given disjoint sources.
		}
		out.append(wgsl + cursor, (size_t)(e.start - cursor));
		out += e.replacement;
		cursor = e.end;
	}
	out.append(wgsl + cursor, (size_t)(len - cursor));

	r_wgsl = out;
	return true;
}

std::vector<TextureSamplerPair> scan_texture_sampler_pairs(const std::string &p_wgsl, const std::vector<ResourceVar> &p_vars) {
	static const char *const FNS[] = { "textureSample", "textureSampleLevel", "textureSampleBias", "textureSampleGrad", "textureGather" };
	std::vector<TextureSamplerPair> out;
	for (const char *fn : FNS) {
		const size_t fn_len = strlen(fn);
		size_t pos = 0;
		while ((pos = p_wgsl.find(fn, pos)) != std::string::npos) {
			size_t after = pos + fn_len;
			bool left_ok = pos == 0 || !_is_ident_char(p_wgsl[pos - 1]);
			pos = after;
			if (!left_ok) {
				continue;
			}
			size_t open = _skip_space(p_wgsl, after);
			if (open >= p_wgsl.size() || p_wgsl[open] != '(') {
				continue; // Also rejects textureSampleCompare etc.: the name goes on to '(' only for the exact function.
			}
			size_t ts = _skip_space(p_wgsl, open + 1);
			size_t te = _read_ident(p_wgsl, ts);
			if (te == ts) {
				continue;
			}
			size_t comma = _skip_space(p_wgsl, te);
			if (comma >= p_wgsl.size() || p_wgsl[comma] != ',') {
				continue;
			}
			size_t ss = _skip_space(p_wgsl, comma + 1);
			size_t se = _read_ident(p_wgsl, ss);
			if (se == ss) {
				continue;
			}
			std::string tex = p_wgsl.substr(ts, te - ts);
			std::string samp = p_wgsl.substr(ss, se - ss);
			const ResourceVar *tv = nullptr;
			const ResourceVar *sv = nullptr;
			for (const ResourceVar &v : p_vars) {
				if (v.name == tex) {
					tv = &v;
				}
				if (v.name == samp) {
					sv = &v;
				}
			}
			if (tv && sv) {
				TextureSamplerPair p;
				p.texture_key = tv->key();
				p.sampler_key = sv->key();
				out.push_back(p);
			}
		}
	}
	return out;
}

// --- Rewrites ---

// Adds p_mip-style ", 0" / renames the target of `textureLoad(NAME, ...)` calls.
// Whitespace around the tokens is free; NAME must be the whole first argument.
static void _rewrite_texture_loads(std::string &r_s, const std::string &p_name, const std::string &p_new_name, bool p_add_mip) {
	static const char *const FN = "textureLoad";
	size_t pos = 0;
	while ((pos = r_s.find(FN, pos)) != std::string::npos) {
		size_t after_fn = pos + strlen(FN);
		if ((pos > 0 && _is_ident_char(r_s[pos - 1])) || (after_fn < r_s.size() && _is_ident_char(r_s[after_fn]))) {
			pos = after_fn;
			continue;
		}
		size_t open = _skip_trivia(r_s, after_fn);
		if (open >= r_s.size() || r_s[open] != '(') {
			pos = after_fn;
			continue;
		}
		size_t ns = _skip_trivia(r_s, open + 1);
		size_t ne = _read_ident(r_s, ns);
		if (ne == ns || r_s.compare(ns, ne - ns, p_name) != 0 || ne - ns != p_name.size()) {
			pos = after_fn;
			continue;
		}
		if (!p_new_name.empty()) {
			r_s.replace(ns, ne - ns, p_new_name);
		}
		if (p_add_mip) {
			size_t close = _match_bracket(r_s, open, '(', ')');
			if (close != std::string::npos) {
				r_s.insert(close, ", 0");
			}
		}
		pos = after_fn;
	}
}

static std::string _sampled_type_for(const ResourceVar &p_v, const std::string &p_format) {
	std::string dim = "texture_" + p_v.type_name.substr(strlen("texture_storage_"));
	const char *comp = "f32";
	if (p_format.find("uint") != std::string::npos) {
		comp = "u32";
	} else if (p_format.find("sint") != std::string::npos) {
		comp = "i32";
	}
	return dim + "<" + comp + ">";
}

std::vector<RWSplit> split_read_write_storage(std::string &r_wgsl, const std::function<bool(const std::string &)> &p_keep_read_write, const std::vector<std::pair<uint32_t, uint32_t>> *p_only) {
	struct Edit {
		size_t type_begin, type_end, insert_at;
		std::string new_type, shadow_decl;
	};
	std::vector<RWSplit> splits;
	std::vector<Edit> edits;
	for (const ResourceVar &v : scan_resource_vars(r_wgsl)) {
		if (v.type_name.compare(0, 16, "texture_storage_") != 0 || v.type_args.size() < 2 || v.type_args[1] != "read_write") {
			continue;
		}
		const std::string &fmt = v.type_args[0];
		RWSplit s;
		s.group = v.group;
		s.binding = v.binding;
		s.var_name = v.name;
		s.format = fmt;
		if (p_only) {
			bool found = false;
			for (const std::pair<uint32_t, uint32_t> &o : *p_only) {
				if (o.first == v.key()) {
					s.shadow_binding = o.second;
					found = true;
					break;
				}
			}
			if (!found) {
				continue;
			}
			s.is_new = false;
		} else {
			if (fmt.empty() || fmt.size() >= 64 || (p_keep_read_write && p_keep_read_write(fmt))) {
				continue;
			}
			s.shadow_binding = v.binding + 1;
		}
		Edit e;
		e.type_begin = v.type_begin;
		e.type_end = v.type_end;
		e.insert_at = v.decl_end + 1;
		e.new_type = v.type_name + "<" + fmt + ", write>";
		e.shadow_decl = "\n@group(" + std::to_string(v.group) + ") @binding(" + std::to_string(s.shadow_binding) + ") \nvar " + v.name + "_rw_in: " + _sampled_type_for(v, fmt) + ";";
		edits.push_back(e);
		splits.push_back(s);
	}
	// Back to front so earlier offsets stay valid.
	for (size_t i = edits.size(); i-- > 0;) {
		r_wgsl.insert(edits[i].insert_at, edits[i].shadow_decl);
		r_wgsl.replace(edits[i].type_begin, edits[i].type_end - edits[i].type_begin, edits[i].new_type);
	}
	for (const RWSplit &s : splits) {
		_rewrite_texture_loads(r_wgsl, s.var_name, s.var_name + "_rw_in", true);
	}
	return splits;
}

std::vector<ReadStorageConversion> convert_read_storage_to_sampled(std::string &r_wgsl) {
	struct Edit {
		size_t type_begin, type_end;
		std::string new_type;
	};
	std::vector<ReadStorageConversion> out;
	std::vector<Edit> edits;
	std::vector<std::string> names;
	for (const ResourceVar &v : scan_resource_vars(r_wgsl)) {
		if (v.type_name.compare(0, 16, "texture_storage_") != 0 || v.type_args.size() < 2 || v.type_args[1] != "read") {
			continue;
		}
		const std::string &fmt = v.type_args[0];
		if (fmt.empty() || fmt.size() >= 64) {
			continue;
		}
		edits.push_back({ v.type_begin, v.type_end, _sampled_type_for(v, fmt) });
		names.push_back(v.name);
		ReadStorageConversion c;
		c.group = v.group;
		c.binding = v.binding;
		c.format = fmt;
		out.push_back(c);
	}
	for (size_t i = edits.size(); i-- > 0;) {
		r_wgsl.replace(edits[i].type_begin, edits[i].type_end - edits[i].type_begin, edits[i].new_type);
	}
	for (const std::string &n : names) {
		_rewrite_texture_loads(r_wgsl, n, std::string(), true);
	}
	return out;
}

} // namespace wgsl_text_scan
