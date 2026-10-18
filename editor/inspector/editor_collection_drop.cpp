/**************************************************************************/
/*  editor_collection_drop.cpp                                            */
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

#include "editor_collection_drop.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/class_db.h"
#include "editor/docks/inspector_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace {

enum class DragKind {
	NONE,
	FILE,
	DIR,
	RESOURCE,
	NODE,
};

struct DraggedItem {
	DragKind kind = DragKind::NONE;
	String path; // FILE, DIR.
	Ref<Resource> resource; // RESOURCE.
	Node *node = nullptr; // NODE.
};

DraggedItem get_dragged_item(const Variant &p_drag_data, int p_index) {
	DraggedItem item;
	if (p_drag_data.get_type() != Variant::DICTIONARY) {
		return item;
	}
	const Dictionary drag_data = p_drag_data;
	const String type = drag_data.get("type", "");
	if (type == "files" || type == "files_and_dirs") {
		const PackedStringArray files = drag_data.get("files", PackedStringArray());
		if (p_index >= 0 && p_index < files.size()) {
			item.path = files[p_index];
			item.kind = item.path.ends_with("/") ? DragKind::DIR : DragKind::FILE;
		}
	} else if (type == "resource" && p_index == 0) {
		item.resource = drag_data.get("resource", Variant());
		item.kind = item.resource.is_valid() ? DragKind::RESOURCE : DragKind::NONE;
	} else if (type == "nodes") {
		const Array nodes = drag_data.get("nodes", Array());
		if (p_index >= 0 && p_index < nodes.size() && SceneTree::get_singleton()) {
			item.node = SceneTree::get_singleton()->get_root()->get_node_or_null(NodePath(nodes[p_index]));
			item.kind = item.node ? DragKind::NODE : DragKind::NONE;
		}
	}
	return item;
}

Vector<String> get_classes(const String &p_hint_string) {
	Vector<String> classes;
	for (const String &class_name : p_hint_string.split(",", false)) {
		const String stripped = class_name.strip_edges();
		if (!stripped.is_empty()) {
			classes.push_back(stripped);
		}
	}
	return classes;
}

bool is_node_class(const String &p_class) {
	if (ClassDB::class_exists(p_class)) {
		return ClassDB::is_parent_class(p_class, SNAME("Node"));
	}
	return EditorNode::get_editor_data().script_class_is_parent(p_class, SNAME("Node"));
}

// Whether an Object slot holds nodes rather than resources.
bool slot_holds_nodes(const EditorCollectionDrop::Slot &p_slot) {
	if (p_slot.hint == PROPERTY_HINT_NODE_TYPE) {
		return true;
	}
	if (p_slot.hint == PROPERTY_HINT_RESOURCE_TYPE) {
		return false;
	}
	const Vector<String> classes = get_classes(p_slot.hint_string);
	if (classes.is_empty()) {
		return false;
	}
	for (const String &class_name : classes) {
		if (!is_node_class(class_name)) {
			return false;
		}
	}
	return true;
}

Vector<String> get_allowed_classes(const EditorCollectionDrop::Slot &p_slot, const String &p_default) {
	Vector<String> classes = get_classes(p_slot.hint_string);
	if (classes.is_empty()) {
		classes.push_back(p_default);
	}
	return classes;
}

// A file's type as the FileSystem dock knows it. Empty when the file is not a resource the editor can load.
bool get_file_type(const String &p_path, StringName &r_type, String &r_script_class) {
	if (!EditorFileSystem::get_singleton()) {
		return false;
	}
	int index = 0;
	const EditorFileSystemDirectory *dir = EditorFileSystem::get_singleton()->find_file(p_path, &index);
	if (!dir) {
		return false;
	}
	r_type = dir->get_file_type(index);
	r_script_class = dir->get_file_resource_script_class(index);
	return r_type != StringName();
}

bool type_allowed(const StringName &p_type, const String &p_script_class, const Vector<String> &p_allowed) {
	for (const String &allowed : p_allowed) {
		if (ClassDB::is_parent_class(p_type, allowed) || EditorNode::get_editor_data().script_class_is_parent(p_script_class, allowed)) {
			return true;
		}
	}
	return false;
}

bool resource_allowed(const Ref<Resource> &p_resource, const Vector<String> &p_allowed) {
	String script_class;
	if (p_resource->get_script()) {
		script_class = EditorNode::get_singleton()->get_object_custom_type_name(p_resource->get_script());
	}
	for (const String &allowed : p_allowed) {
		if (p_resource->is_class(allowed) || EditorNode::get_editor_data().script_class_is_parent(script_class, allowed)) {
			return true;
		}
	}
	return false;
}

bool node_allowed(const Node *p_node, const Vector<String> &p_allowed) {
	for (const String &allowed : p_allowed) {
		if (p_node->is_class(allowed) || EditorNode::get_singleton()->is_object_of_custom_type(p_node, allowed)) {
			return true;
		}
	}
	return false;
}

bool is_file_hint(PropertyHint p_hint) {
	return p_hint == PROPERTY_HINT_FILE || p_hint == PROPERTY_HINT_FILE_PATH || p_hint == PROPERTY_HINT_SAVE_FILE || p_hint == PROPERTY_HINT_GLOBAL_FILE || p_hint == PROPERTY_HINT_GLOBAL_SAVE_FILE;
}

bool is_dir_hint(PropertyHint p_hint) {
	return p_hint == PROPERTY_HINT_DIR || p_hint == PROPERTY_HINT_GLOBAL_DIR;
}

// Whether a String slot takes this file's path: plain strings take any file, file hints the ones their filters allow.
bool string_slot_accepts_file(const EditorCollectionDrop::Slot &p_slot, const String &p_path) {
	if (p_slot.hint == PROPERTY_HINT_NONE || p_slot.hint == PROPERTY_HINT_PLACEHOLDER_TEXT || p_slot.hint == PROPERTY_HINT_MULTILINE_TEXT) {
		return true;
	}
	if (!is_file_hint(p_slot.hint)) {
		return false;
	}
	bool has_filter = false;
	for (const String &entry : p_slot.hint_string.split(",", false)) {
		const String filter = entry.get_slicec(';', 0).strip_edges(); // "*.png;PNG Images" -> "*.png".
		if (filter.is_empty()) {
			continue;
		}
		has_filter = true;
		if (p_path.get_file().matchn(filter)) {
			return true;
		}
	}
	return !has_filter;
}

bool string_slot_accepts_dir(const EditorCollectionDrop::Slot &p_slot) {
	return is_dir_hint(p_slot.hint);
}

// The path a single property with this hint would store (see EditorPropertyPath::_path_selected()).
Variant path_value(const EditorCollectionDrop::Slot &p_slot, const String &p_path) {
	String path = p_path;
	if (p_slot.hint == PROPERTY_HINT_FILE) {
		const ResourceUID::ID id = ResourceLoader::get_resource_uid(path);
		if (id != ResourceUID::INVALID_ID) {
			path = ResourceUID::get_singleton()->id_to_text(id);
		}
	} else if (p_slot.hint == PROPERTY_HINT_GLOBAL_FILE || p_slot.hint == PROPERTY_HINT_GLOBAL_SAVE_FILE || p_slot.hint == PROPERTY_HINT_GLOBAL_DIR) {
		path = ProjectSettings::get_singleton()->globalize_path(path);
	}
	if (p_slot.type == Variant::STRING_NAME) {
		return StringName(path);
	}
	return path;
}

NodePath relative_node_path(const Node *p_node, const Node *p_base_node) {
	if (p_base_node) {
		return p_base_node->get_path().rel_path_to(p_node->get_path());
	}
	return p_node->get_path();
}

} // namespace

bool EditorCollectionDrop::is_collection_drag(const Variant &p_drag_data) {
	return get_item_count(p_drag_data) > 0;
}

int EditorCollectionDrop::get_item_count(const Variant &p_drag_data) {
	if (p_drag_data.get_type() != Variant::DICTIONARY) {
		return 0;
	}
	const Dictionary drag_data = p_drag_data;
	const String type = drag_data.get("type", "");
	if (type == "files" || type == "files_and_dirs") {
		return PackedStringArray(drag_data.get("files", PackedStringArray())).size();
	} else if (type == "resource") {
		return Ref<Resource>(drag_data.get("resource", Variant())).is_valid() ? 1 : 0;
	} else if (type == "nodes") {
		return Array(drag_data.get("nodes", Array())).size();
	}
	return 0;
}

bool EditorCollectionDrop::can_accept_item(const Variant &p_drag_data, int p_index, const Slot &p_slot, const Node *p_base_node) {
	const DraggedItem item = get_dragged_item(p_drag_data, p_index);
	const bool string_slot = p_slot.type == Variant::STRING || p_slot.type == Variant::STRING_NAME;

	switch (item.kind) {
		case DragKind::FILE: {
			if (string_slot) {
				return string_slot_accepts_file(p_slot, item.path);
			}
			StringName type;
			String script_class;
			if (p_slot.type == Variant::NIL) {
				return get_file_type(item.path, type, script_class);
			}
			if (p_slot.type == Variant::OBJECT && !slot_holds_nodes(p_slot)) {
				return get_file_type(item.path, type, script_class) && type_allowed(type, script_class, get_allowed_classes(p_slot, "Resource"));
			}
			return false;
		}
		case DragKind::DIR: {
			return string_slot && string_slot_accepts_dir(p_slot);
		}
		case DragKind::RESOURCE: {
			if (p_slot.type == Variant::NIL) {
				return true;
			}
			if (p_slot.type == Variant::OBJECT && !slot_holds_nodes(p_slot)) {
				return resource_allowed(item.resource, get_allowed_classes(p_slot, "Resource"));
			}
			if (string_slot) {
				// Only a resource saved to its own file has a path to store.
				return item.resource->get_path().is_resource_file() && string_slot_accepts_file(p_slot, item.resource->get_path());
			}
			return false;
		}
		case DragKind::NODE: {
			if (p_slot.type == Variant::NIL) {
				return true;
			}
			if (p_slot.type == Variant::NODE_PATH) {
				const Vector<String> classes = p_slot.hint == PROPERTY_HINT_NODE_PATH_VALID_TYPES ? get_classes(p_slot.hint_string) : Vector<String>();
				return classes.is_empty() || node_allowed(item.node, classes);
			}
			if (p_slot.type == Variant::OBJECT && slot_holds_nodes(p_slot)) {
				return node_allowed(item.node, get_allowed_classes(p_slot, "Node"));
			}
			return false;
		}
		case DragKind::NONE: {
			return false;
		}
	}
	return false;
}

bool EditorCollectionDrop::can_accept(const Variant &p_drag_data, const Slot &p_slot, const Node *p_base_node) {
	const int count = get_item_count(p_drag_data);
	if (count == 0) {
		return false;
	}
	for (int i = 0; i < count; i++) {
		if (!can_accept_item(p_drag_data, i, p_slot, p_base_node)) {
			return false;
		}
	}
	return true;
}

EditorCollectionDrop::Item EditorCollectionDrop::resolve_item(const Variant &p_drag_data, int p_index, const Slot &p_slot, Node *p_base_node) {
	const DraggedItem dragged = get_dragged_item(p_drag_data, p_index);
	const bool string_slot = p_slot.type == Variant::STRING || p_slot.type == Variant::STRING_NAME;
	Item item;

	switch (dragged.kind) {
		case DragKind::FILE: {
			item.name = dragged.path.get_file().get_basename();
			item.value = string_slot ? path_value(p_slot, dragged.path) : Variant(ResourceLoader::load(dragged.path));
		} break;
		case DragKind::DIR: {
			item.name = dragged.path.trim_suffix("/").get_file();
			item.value = path_value(p_slot, dragged.path);
		} break;
		case DragKind::RESOURCE: {
			const String path = dragged.resource->get_path();
			item.name = !dragged.resource->get_name().is_empty() ? dragged.resource->get_name() : (path.is_resource_file() ? path.get_file().get_basename() : dragged.resource->get_class());
			item.value = string_slot ? path_value(p_slot, path) : Variant(dragged.resource);
		} break;
		case DragKind::NODE: {
			item.name = dragged.node->get_name();
			if (p_slot.type == Variant::OBJECT) {
				item.value = dragged.node;
			} else {
				item.value = relative_node_path(dragged.node, p_base_node);
			}
		} break;
		case DragKind::NONE: {
		} break;
	}
	return item;
}

Vector<EditorCollectionDrop::Item> EditorCollectionDrop::resolve(const Variant &p_drag_data, const Slot &p_slot, Node *p_base_node) {
	Vector<Item> items;
	const int count = get_item_count(p_drag_data);
	for (int i = 0; i < count; i++) {
		Item item = resolve_item(p_drag_data, i, p_slot, p_base_node);
		// A file that failed to load gives no item rather than a null element.
		if (item.value.get_type() == Variant::NIL || (item.value.get_type() == Variant::OBJECT && !item.value.get_validated_object())) {
			continue;
		}
		items.push_back(item);
	}
	return items;
}

Node *EditorCollectionDrop::get_base_node(const Node *p_from) {
	Node *base_node = Object::cast_to<Node>(InspectorDock::get_inspector_singleton()->get_edited_object());
	if (!base_node && p_from && p_from->is_inside_tree()) {
		base_node = p_from->get_tree()->get_edited_scene_root();
	}
	return base_node;
}
