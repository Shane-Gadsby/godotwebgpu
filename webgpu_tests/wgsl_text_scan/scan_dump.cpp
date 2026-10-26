/**************************************************************************/
/*  scan_dump.cpp                                                         */
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

// Runs every WGSL text consumer of the driver (drivers/webgpu/wgsl_text_scan.*) over one WGSL file
// and prints what they derive, one canonical line per fact, with no identifier names in it (Tint and
// SPIRV-Cross name things differently; only the answers must agree). run_tests.sh diffs the output
// for the two converters' WGSL of the same SPIR-V.
//
//   scan_dump <file.wgsl> [--keep-rw] [--emit <out.wgsl>]   (--emit: also write the text after the rewrites)
//
// The order is the driver's: format-independent rewrites first (read_write split, read-only to
// sampled, depth reclassification), then the declaration scans over the rewritten text.
#include "wgsl_text_scan.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace wgsl_text_scan;

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: scan_dump <file.wgsl> [--keep-rw]\n");
		return 2;
	}
	bool keep_rw = false;
	const char *emit_path = nullptr;
	for (int i = 2; i < argc; i++) {
		if (std::string(argv[i]) == "--keep-rw") {
			keep_rw = true;
		} else if (std::string(argv[i]) == "--emit" && i + 1 < argc) {
			emit_path = argv[++i];
		}
	}
	std::ifstream f(argv[1], std::ios::binary);
	if (!f) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	std::string wgsl = ss.str();

	std::vector<std::string> lines;
	auto emit = [&](const std::string &p_line) { lines.push_back(p_line); };
	auto key_str = [](uint32_t g, uint32_t b) { return std::to_string(g) + "/" + std::to_string(b); };

	std::set<uint32_t> exclude;
	std::vector<RWSplit> splits = split_read_write_storage(wgsl, [&](const std::string &fmt) { return keep_rw && (fmt == "r32float" || fmt == "r32uint" || fmt == "r32sint"); });
	for (const RWSplit &s : splits) {
		emit("RWSPLIT " + key_str(s.group, s.binding) + " shadow=" + std::to_string(s.shadow_binding) + " fmt=" + s.format);
		exclude.insert((s.group << 16) | s.shadow_binding);
	}
	for (const ReadStorageConversion &c : convert_read_storage_to_sampled(wgsl)) {
		emit("ROCONV " + key_str(c.group, c.binding) + " fmt=" + c.format);
		exclude.insert((c.group << 16) | c.binding);
	}
	std::set<uint32_t> before_depth;
	for (const ResourceVar &v : scan_resource_vars(wgsl)) {
		if (describe(v).is_depth) {
			before_depth.insert(v.key());
		}
	}
	reclassify_single_component_depth_textures(wgsl, exclude);

	if (emit_path) {
		std::ofstream o(emit_path, std::ios::binary);
		o << wgsl;
	}
	std::vector<ResourceVar> vars = scan_resource_vars(wgsl);
	for (const ResourceVar &v : vars) {
		Description d = describe(v);
		std::string l = "VAR " + key_str(v.group, v.binding);
		if (d.is_sampler) {
			l += d.is_comparison_sampler ? " sampler_comparison" : " sampler";
		}
		if (d.is_texture) {
			l += " tex dim=" + std::to_string(d.dim) + " depth=" + std::to_string(d.is_depth) + " ms=" + std::to_string(d.is_multisampled) + " sample=" + std::to_string(d.sample_kind);
		}
		if (d.is_storage_texture) {
			l += " storage fmt=" + d.storage_format + " access=" + d.storage_access;
		}
		if (d.is_uniform) {
			l += " uniform";
		}
		if (d.is_storage_buffer) {
			l += d.storage_read_only ? " ssbo_ro" : " ssbo_rw";
		}
		emit(l);
		if (d.is_depth && !before_depth.count(v.key())) {
			emit("DEPTH_RECLASSIFIED " + key_str(v.group, v.binding));
		}
	}
	for (const TextureSamplerPair &p : scan_texture_sampler_pairs(wgsl, vars)) {
		emit("PAIR " + key_str(p.texture_key >> 16, p.texture_key & 0xFFFF) + " " + key_str(p.sampler_key >> 16, p.sampler_key & 0xFFFF));
	}
	for (const OverrideDecl &o : scan_overrides(wgsl)) {
		emit("OVERRIDE id=" + std::to_string(o.id) + " override_kw=" + std::to_string(o.followed_by_override) + " used=" + std::to_string(o.name_used));
	}
	std::sort(lines.begin(), lines.end());
	lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
	for (const std::string &l : lines) {
		std::cout << l << "\n";
	}
	return 0;
}
