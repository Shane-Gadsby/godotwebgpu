/**************************************************************************/
/*  editor_collection_drop.h                                              */
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

#include "core/object/object.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

class Node;

// Turns what is dragged from the FileSystem dock (one or many files or folders), from a resource picker (a
// resource) or from the Scene dock (one or many nodes) into values for one slot of a collection: an Array element,
// a Dictionary key or value, or an element property of a property array. Every collection editor goes through it,
// so they all accept the same drops and store the same values a single property of that type would:
//
// - Object slots holding resources get the loaded resources, if their class (or script class) is allowed;
//   slots holding nodes get the nodes.
// - String and StringName slots get paths: a uid:// for PROPERTY_HINT_FILE, res:// otherwise, absolute paths for
//   the global hints, folders for the directory hints, honoring the hint's extension filters.
// - NodePath slots get paths relative to the edited node, for the node types the hint allows.
// - Untyped (Variant) slots get resources for files and resources, NodePaths for nodes.
class EditorCollectionDrop {
public:
	struct Slot {
		Variant::Type type = Variant::NIL;
		PropertyHint hint = PROPERTY_HINT_NONE;
		String hint_string;

		Slot() {}
		Slot(Variant::Type p_type, PropertyHint p_hint, const String &p_hint_string) :
				type(p_type), hint(p_hint), hint_string(p_hint_string) {}
		static Slot from_property(const PropertyInfo &p_property) { return Slot(p_property.type, p_property.hint, p_property.hint_string); }
	};

	struct Item {
		Variant value;
		String name; // The file, resource or node name: a readable Dictionary key.
	};

	// Whether the drag data is something this resolves (files, folders, a resource, nodes) and holds at least one item.
	static bool is_collection_drag(const Variant &p_drag_data);
	static int get_item_count(const Variant &p_drag_data);

	// Whether dragged item `p_index` fits `p_slot`. Loads nothing.
	static bool can_accept_item(const Variant &p_drag_data, int p_index, const Slot &p_slot, const Node *p_base_node);
	// Whether every dragged item fits `p_slot`.
	static bool can_accept(const Variant &p_drag_data, const Slot &p_slot, const Node *p_base_node);
	// The value of dragged item `p_index` for `p_slot` (loading a file where the slot holds resources). Call only when
	// can_accept_item() is true.
	static Item resolve_item(const Variant &p_drag_data, int p_index, const Slot &p_slot, Node *p_base_node);
	// Every dragged item's value. Call only when can_accept() is true.
	static Vector<Item> resolve(const Variant &p_drag_data, const Slot &p_slot, Node *p_base_node);

	// The node the inspector edits, or the edited scene's root: what NodePaths are relative to.
	static Node *get_base_node(const Node *p_from);
};
