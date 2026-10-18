/**************************************************************************/
/*  gdscript_braced_conversion.h                                          */
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

// GDB: "Convert to Braced GDScript (.gdb)" and "Convert to GDScript (.gd)" in the FileSystem dock.

#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/plugins/editor_plugin.h"

class GDScriptBracedConversion {
public:
	struct Report {
		Vector<String> converted; // "old -> new" per file.
		Vector<String> errors;
		int references_updated = 0;
	};

	// Converts the scripts at `p_paths` (files, or folders searched recursively) to braced or indented GDScript.
	// Each file is renamed through the FileSystem dock, which updates the scenes, resources and project settings that
	// use it, and then path strings in other scripts (`preload`, `load`, `extends`) are updated too.
	static Report convert(const Vector<String> &p_paths, bool p_to_braced);

	// Rewrites the string literals in `p_code` that name a key of `p_renames`, absolute (`res://`) or relative to
	// `p_script_path`, keeping the form they were written in. Returns how many it changed.
	static int update_path_strings(String &r_code, const String &p_script_path, const HashMap<String, String> &p_renames);
};

class GDScriptConversionContextMenuPlugin : public EditorContextMenuPlugin {
	GDCLASS(GDScriptConversionContextMenuPlugin, EditorContextMenuPlugin);

	void _convert(const Variant &p_data, bool p_to_braced);
	void _convert_to_braced(const Variant &p_data) { _convert(p_data, true); }
	void _convert_to_indented(const Variant &p_data) { _convert(p_data, false); }

public:
	virtual void get_options(const OptionsData &p_data) override;
};

class GDScriptBracedEditorPlugin : public EditorPlugin {
	GDCLASS(GDScriptBracedEditorPlugin, EditorPlugin);

public:
	virtual String get_plugin_name() const override { return "GDScriptBraced"; }

	GDScriptBracedEditorPlugin();
};
