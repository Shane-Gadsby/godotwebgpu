/**************************************************************************/
/*  editor_default_folders.h                                              */
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

#include "core/string/string_name.h"
#include "core/string/ustring.h"

class CheckBox;

// Per-project default folders for files created in the editor (the `editor/default_folders/*` project
// settings). A create or save dialog that is not tied to a specific folder proposes the default folder of
// the category its resource type falls in, and offers a check box that makes the chosen folder the new default.
class EditorDefaultFolders {
public:
	enum Category {
		CATEGORY_NONE = -1,
		CATEGORY_SCRIPTS,
		CATEGORY_MATERIALS,
		CATEGORY_SHADERS,
		CATEGORY_SCENES,
		CATEGORY_SETTINGS,
		CATEGORY_RESOURCES,
		CATEGORY_MAX,
	};

	static void register_settings();

	static String get_setting_name(Category p_category);
	static Category get_category_for_type(const StringName &p_type);

	// The default folder, or an empty string if none is set or it does not exist.
	static String get_folder(Category p_category);
	// `p_path`'s file name in the default folder, or `p_path` unchanged if there is no default folder.
	static String apply_folder(Category p_category, const String &p_path);
	static void set_folder(Category p_category, const String &p_folder);

	static String get_remember_text(Category p_category);
	static CheckBox *create_remember_check_box(Category p_category);
	// Unticks the check box and describes the current default in its tooltip.
	static void reset_remember_check_box(CheckBox *p_check_box, Category p_category);
};
