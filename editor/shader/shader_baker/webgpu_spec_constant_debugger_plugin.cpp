/**************************************************************************/
/*  webgpu_spec_constant_debugger_plugin.cpp                              */
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

#include "webgpu_spec_constant_debugger_plugin.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_preset.h"

namespace {
// Matches the file a developer would otherwise download manually via
// godotWebGPUExportSpecConstantRecording() -- see this file's header doc
// comment. Kept under .godot/ alongside the other internal engine caches
// this fork already stores there (e.g. the WebGPU shader cache), not under
// the visible project tree.
const char *CAPTURED_USAGE_FILE_PATH = "res://.godot/webgpu_spec_constant_usage_captured.json";
} // namespace

bool WebGPUSpecConstantDebuggerPlugin::has_capture(const String &p_capture) const {
	return p_capture == "webgpu";
}

bool WebGPUSpecConstantDebuggerPlugin::capture(const String &p_message, const Array &p_data, int p_session) {
	if (p_message != "webgpu:spec_constant_usage" || p_data.is_empty()) {
		return false;
	}

	Dictionary entry = p_data[0];

	// The debugger wire protocol carries "base_spv_hash" as its raw bit
	// pattern reinterpreted as int64_t (see _record_spec_constant_usage()'s
	// doc comment in rendering_device_driver_webgpu.cpp) -- a real
	// Variant::INT at this point. Convert it to a precise decimal STRING
	// immediately, matching the format godotWebGPUExportSpecConstantRecording()
	// already uses (and that the export-time baker's parse_uint64_decimal()
	// already expects): this function re-reads and re-JSON-parses the whole
	// growing accumulator file below on every single capture, and Godot's
	// JSON grammar has no integer type -- JSON::parse_string() always
	// produces Variant::FLOAT for a bare JSON number (see json.cpp's
	// TK_NUMBER handling), and a double's 53-bit mantissa cannot exactly
	// represent most 64-bit hashes. A quoted string survives any number of
	// JSON round-trips unchanged; a bare number silently corrupts within a
	// single capture session (confirmed: every auto-captured hash observed
	// in practice had already drifted by the second entry).
	if (entry.has("base_spv_hash")) {
		uint64_t base_spv_hash = (uint64_t)(int64_t)entry["base_spv_hash"];
		entry["base_spv_hash"] = String::num_uint64(base_spv_hash);
	}

	Array entries;
	Ref<FileAccess> read_f = FileAccess::open(CAPTURED_USAGE_FILE_PATH, FileAccess::READ);
	if (read_f.is_valid()) {
		Variant parsed = JSON::parse_string(read_f->get_as_text());
		if (parsed.get_type() == Variant::ARRAY) {
			entries = parsed;
		}
	}
	entries.push_back(entry);

	Ref<FileAccess> write_f = FileAccess::open(CAPTURED_USAGE_FILE_PATH, FileAccess::WRITE);
	if (write_f.is_valid()) {
		write_f->store_string(JSON::stringify(entries, "  "));
	} else {
		WARN_PRINT(vformat("WebGPU: could not write captured specialization-constant usage to '%s'.", String(CAPTURED_USAGE_FILE_PATH)));
	}

	// Auto-point every Web export preset that doesn't already have a usage
	// file configured at this one, so capturing "just works" without the
	// developer needing to separately open the export preset dialog.
	// Existing non-empty values are left alone -- if someone already pointed
	// it somewhere deliberately, this shouldn't silently override that.
	EditorExport *editor_export = EditorExport::get_singleton();
	if (editor_export) {
		for (int i = 0; i < editor_export->get_export_preset_count(); i++) {
			Ref<EditorExportPreset> preset = editor_export->get_export_preset(i);
			if (preset.is_null()) {
				continue;
			}
			Ref<EditorExportPlatform> platform = preset->get_platform();
			if (platform.is_null() || platform->get_name() != "Web") {
				continue;
			}
			String existing = preset->get("shader_baker/spec_constant_usage_file");
			if (existing.is_empty()) {
				preset->set("shader_baker/spec_constant_usage_file", String(CAPTURED_USAGE_FILE_PATH));
			}
		}
	}

	return true;
}
