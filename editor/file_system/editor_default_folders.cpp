/**************************************************************************/
/*  editor_default_folders.cpp                                            */
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

#include "editor_default_folders.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/object/class_db.h"
#include "editor/gui/editor_toaster.h"
#include "scene/gui/check_box.h"

// Built-in resources that configure the project, a scene or the UI rather than being content.
static const char *settings_types[] = {
	"Environment",
	"CameraAttributes",
	"Compositor",
	"Theme",
	"AudioBusLayout",
	"Shortcut",
	"InputEvent",
	"PhysicsMaterial",
};

static String _get_category_name(EditorDefaultFolders::Category p_category) {
	switch (p_category) {
		case EditorDefaultFolders::CATEGORY_SCRIPTS:
			return TTR("scripts");
		case EditorDefaultFolders::CATEGORY_MATERIALS:
			return TTR("materials");
		case EditorDefaultFolders::CATEGORY_SHADERS:
			return TTR("shaders");
		case EditorDefaultFolders::CATEGORY_SCENES:
			return TTR("scenes");
		case EditorDefaultFolders::CATEGORY_SETTINGS:
			return TTR("settings");
		case EditorDefaultFolders::CATEGORY_RESOURCES:
			return TTR("resources");
		default:
			return String();
	}
}

void EditorDefaultFolders::register_settings() {
	for (int i = 0; i < CATEGORY_MAX; i++) {
		GLOBAL_DEF(PropertyInfo(Variant::STRING, get_setting_name(Category(i)), PROPERTY_HINT_DIR), "");
	}

	// The script folder used to be the only one, under `editor/script/`.
	const String legacy_setting = "editor/script/default_script_folder";
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings->has_setting(legacy_setting)) {
		const String legacy_folder = project_settings->get_setting(legacy_setting);
		const String scripts_setting = get_setting_name(CATEGORY_SCRIPTS);
		if (String(project_settings->get_setting(scripts_setting)).is_empty()) {
			project_settings->set_setting(scripts_setting, legacy_folder);
		}
		project_settings->set_setting(legacy_setting, Variant());
	}
}

String EditorDefaultFolders::get_setting_name(Category p_category) {
	switch (p_category) {
		case CATEGORY_SCRIPTS:
			return "editor/default_folders/scripts";
		case CATEGORY_MATERIALS:
			return "editor/default_folders/materials";
		case CATEGORY_SHADERS:
			return "editor/default_folders/shaders";
		case CATEGORY_SCENES:
			return "editor/default_folders/scenes";
		case CATEGORY_SETTINGS:
			return "editor/default_folders/settings";
		case CATEGORY_RESOURCES:
			return "editor/default_folders/resources";
		default:
			ERR_FAIL_V(String());
	}
}

EditorDefaultFolders::Category EditorDefaultFolders::get_category_for_type(const StringName &p_type) {
	if (ClassDB::is_parent_class(p_type, "Script")) {
		return CATEGORY_SCRIPTS;
	}
	if (ClassDB::is_parent_class(p_type, "Shader") || ClassDB::is_parent_class(p_type, "ShaderInclude")) {
		return CATEGORY_SHADERS;
	}
	if (ClassDB::is_parent_class(p_type, "Material")) {
		return CATEGORY_MATERIALS;
	}
	if (ClassDB::is_parent_class(p_type, "PackedScene")) {
		return CATEGORY_SCENES;
	}
	for (const char *settings_type : settings_types) {
		if (ClassDB::is_parent_class(p_type, settings_type)) {
			return CATEGORY_SETTINGS;
		}
	}
	if (ClassDB::is_parent_class(p_type, "Resource")) {
		return CATEGORY_RESOURCES;
	}
	return CATEGORY_NONE;
}

String EditorDefaultFolders::get_folder(Category p_category) {
	ERR_FAIL_INDEX_V(p_category, CATEGORY_MAX, String());

	const String folder = GLOBAL_GET(get_setting_name(p_category));
	if (folder.is_empty() || !DirAccess::dir_exists_absolute(folder)) {
		return String();
	}
	return folder;
}

String EditorDefaultFolders::apply_folder(Category p_category, const String &p_path) {
	if (p_path.is_empty() || p_category == CATEGORY_NONE) {
		return p_path;
	}

	const String folder = get_folder(p_category);
	if (folder.is_empty()) {
		return p_path;
	}
	return folder.path_join(p_path.get_file());
}

void EditorDefaultFolders::set_folder(Category p_category, const String &p_folder) {
	ERR_FAIL_INDEX(p_category, CATEGORY_MAX);

	const String folder = ProjectSettings::get_singleton()->localize_path(p_folder);
	ERR_FAIL_COND_MSG(!folder.begins_with("res://"), vformat("Default folder \"%s\" is not inside the project.", p_folder));

	const String setting = get_setting_name(p_category);
	if (String(GLOBAL_GET(setting)) == folder) {
		return;
	}
	ProjectSettings::get_singleton()->set_setting(setting, folder);
	ProjectSettings::get_singleton()->save();
	EditorToaster::get_singleton()->popup_str(vformat(TTR("New %s will be created in \"%s\" by default."), _get_category_name(p_category), folder));
}

String EditorDefaultFolders::get_remember_text(Category p_category) {
	return vformat(TTR("Make this the default folder for %s"), _get_category_name(p_category));
}

CheckBox *EditorDefaultFolders::create_remember_check_box(Category p_category) {
	CheckBox *check_box = memnew(CheckBox);
	check_box->set_text(get_remember_text(p_category));
	reset_remember_check_box(check_box, p_category);
	return check_box;
}

void EditorDefaultFolders::reset_remember_check_box(CheckBox *p_check_box, Category p_category) {
	ERR_FAIL_NULL(p_check_box);

	p_check_box->set_pressed(false);
	const String folder = get_folder(p_category);
	const String current = folder.is_empty() ? TTR("No default folder is set.") : vformat(TTR("The current default folder is \"%s\"."), folder);
	p_check_box->set_tooltip_text(current + "\n" + vformat(TTR("Set in Project Settings > Editor > Default Folders (%s)."), get_setting_name(p_category)));
}
