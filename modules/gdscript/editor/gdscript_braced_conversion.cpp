/**************************************************************************/
/*  gdscript_braced_conversion.cpp                                        */
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

#include "gdscript_braced_conversion.h"

#include "../gdscript.h"
#include "../gdscript_formatter.h"
#include "../gdscript_tokenizer.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_toaster.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/themes/editor_scale.h"

namespace {

void _collect(const String &p_path, Vector<String> &r_files) {
	if (!p_path.ends_with("/") && FileAccess::exists(p_path)) {
		if (GDScriptLanguage::is_gdscript_path(p_path)) {
			r_files.push_back(p_path);
		}
		return;
	}
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		const String full = p_path.path_join(name);
		if (dir->current_is_dir()) {
			_collect(full, r_files);
		} else if (GDScriptLanguage::is_gdscript_path(full)) {
			r_files.push_back(full);
		}
	}
	dir->list_dir_end();
}

} // namespace

int GDScriptBracedConversion::update_path_strings(String &r_code, const String &p_script_path, const HashMap<String, String> &p_renames) {
	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(r_code);
#ifdef GDSCRIPT_BRACES_ENABLED
	tokenizer.set_brace_mode(GDScriptTokenizer::is_braced_path(p_script_path));
#endif // GDSCRIPT_BRACES_ENABLED
	tokenizer.set_multiline_mode(true);

	Vector<int> line_starts;
	line_starts.push_back(0);
	for (int i = 0; i < r_code.length(); i++) {
		if (r_code[i] == '\n') {
			line_starts.push_back(i + 1);
		}
	}
	struct Replacement {
		int start = 0;
		int end = 0;
		String text;
	};
	Vector<Replacement> replacements;
	const String base_dir = p_script_path.get_base_dir();
	for (GDScriptTokenizer::Token token = tokenizer.scan(); token.type != GDScriptTokenizer::Token::TK_EOF; token = tokenizer.scan()) {
		if (token.type != GDScriptTokenizer::Token::LITERAL || token.literal.get_type() != Variant::STRING || token.source.is_empty()) {
			continue;
		}
		const String value = token.literal;
		const bool relative = !value.contains("://");
		const String absolute = relative ? base_dir.path_join(value).simplify_path() : value;
		const String *renamed = p_renames.getptr(absolute);
		if (renamed == nullptr) {
			continue;
		}
		// A conversion only changes the extension: keep the string as written and swap that.
		String new_value;
		if (renamed->get_basename() == absolute.get_basename()) {
			new_value = value.get_basename() + "." + renamed->get_extension();
		} else {
			new_value = relative ? base_dir.path_to_file(*renamed) : *renamed;
		}
		const char32_t quote = token.source[token.source.length() - 1];
		Replacement replacement;
		replacement.start = line_starts[token.start_line - 1] + token.start_column - 1;
		replacement.end = line_starts[token.end_line - 1] + token.end_column - 1;
		replacement.text = String::chr(quote) + new_value + String::chr(quote);
		replacements.push_back(replacement);
	}
	for (int i = replacements.size() - 1; i >= 0; i--) {
		const Replacement &replacement = replacements[i];
		r_code = r_code.substr(0, replacement.start) + replacement.text + r_code.substr(replacement.end);
	}
	return replacements.size();
}

GDScriptBracedConversion::Report GDScriptBracedConversion::convert(const Vector<String> &p_paths, bool p_to_braced) {
	Report report;
	Vector<String> files;
	for (const String &path : p_paths) {
		_collect(path, files);
	}

	// An unsaved editor buffer would overwrite the conversion when saved.
	ScriptEditor::get_singleton()->save_all_scripts();

	HashMap<String, String> renames;
	for (const String &file : files) {
		if (GDScriptTokenizer::is_braced_path(file) == p_to_braced) {
			continue;
		}
		const String target = file.get_basename() + (p_to_braced ? ".gdb" : ".gd");
		if (FileAccess::exists(target)) {
			report.errors.push_back(vformat(TTR("%s: %s already exists."), file, target.get_file()));
			continue;
		}
		Error err = OK;
		const String source = FileAccess::get_file_as_string(file, &err);
		if (err != OK) {
			report.errors.push_back(vformat(TTR("%s: cannot read the file."), file));
			continue;
		}
		const String os_target = ProjectSettings::get_singleton()->globalize_path(target);
		const GDScriptFormatter::Result result = GDScriptFormatter::convert(source, file, false, GDScriptFormatter::get_options_for_path(os_target));
		if (result.error != OK) {
			report.errors.push_back(result.line > 0 ? vformat("%s:%d: %s", file, result.line, result.message) : vformat("%s: %s", file, result.message));
			continue;
		}
		if (FileSystemDock::get_singleton()->rename_file(file, target) != OK) {
			report.errors.push_back(vformat(TTR("%s: cannot rename the file."), file));
			continue;
		}
		Ref<FileAccess> f = FileAccess::open(target, FileAccess::WRITE, &err);
		if (f.is_null()) {
			report.errors.push_back(vformat(TTR("%s: renamed, but the converted code could not be written."), target));
			continue;
		}
		f->store_string(result.output);
		f.unref();
		renames[file] = target;
		report.converted.push_back(file + " -> " + target.get_file());
	}

	// Path strings in every script of the project.
	if (!renames.is_empty()) {
		Vector<String> scripts;
		_collect("res://", scripts);
		for (const String &script : scripts) {
			String code = FileAccess::get_file_as_string(script);
			const int changed = update_path_strings(code, script, renames);
			if (changed > 0) {
				Ref<FileAccess> f = FileAccess::open(script, FileAccess::WRITE);
				if (f.is_valid()) {
					f->store_string(code);
					report.references_updated += changed;
				}
			}
		}
	}

	ScriptEditor::get_singleton()->reload_scripts();
	EditorFileSystem::get_singleton()->scan_changes();
	return report;
}

void GDScriptConversionContextMenuPlugin::get_options(const OptionsData &p_data) {
	EditorContextMenuPlugin::get_options(p_data);
	if (!p_data.has("selected_files")) {
		return;
	}
	const PackedStringArray selected = p_data["selected_files"];
	bool has_indented = false;
	bool has_braced = false;
	for (const String &path : selected) {
		if (path.ends_with("/")) {
			has_indented = has_braced = true; // A folder may hold either.
		} else if (GDScriptTokenizer::is_braced_path(path)) {
			has_braced = true;
		} else if (path.has_extension("gd")) {
			has_indented = true;
		}
	}
	const Ref<Texture2D> icon = EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("GDScript"), SNAME("EditorIcons"));
	if (has_indented) {
		add_context_menu_item(TTR("Convert to Braced GDScript (.gdb)"), callable_mp(this, &GDScriptConversionContextMenuPlugin::_convert_to_braced), icon);
	}
	if (has_braced) {
		add_context_menu_item(TTR("Convert to GDScript (.gd)"), callable_mp(this, &GDScriptConversionContextMenuPlugin::_convert_to_indented), icon);
	}
}

void GDScriptConversionContextMenuPlugin::_convert(const Variant &p_data, bool p_to_braced) {
	const Dictionary data = p_data;
	if (!data.has("selected_files")) {
		return;
	}
	const PackedStringArray selected = data["selected_files"];
	Vector<String> paths;
	for (const String &path : selected) {
		paths.push_back(path);
	}
	const GDScriptBracedConversion::Report report = GDScriptBracedConversion::convert(paths, p_to_braced);

	String message = vformat(TTR("Converted %d script(s)."), report.converted.size());
	if (report.references_updated > 0) {
		message += " " + vformat(TTR("Updated %d path(s) in other scripts."), report.references_updated);
	}
	if (report.errors.is_empty()) {
		EditorToaster::get_singleton()->popup_str(message, EditorToaster::SEVERITY_INFO);
		return;
	}
	message += "\n\n" + TTR("Not converted:") + "\n" + String("\n").join(report.errors);
	EditorNode::get_singleton()->show_warning(message, TTR("Script Conversion"));
}

GDScriptBracedEditorPlugin::GDScriptBracedEditorPlugin() {
	Ref<GDScriptConversionContextMenuPlugin> menu;
	menu.instantiate();
	add_context_menu_plugin(EditorContextMenuPlugin::CONTEXT_SLOT_FILESYSTEM, menu);
}
