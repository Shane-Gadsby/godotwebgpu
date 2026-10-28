/**************************************************************************/
/*  metahuman_clothing_binds.h                                            */
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

#include "editor/import/3d/resource_importer_scene.h"

class ImporterMesh;

// How many vertices of p_mesh each skin bind weights (weight above zero), for binds [0, p_bind_count).
// A bind no vertex is weighted to moves nothing, so whether it matches a bone does not matter.
LocalVector<int> metahuman_count_bind_weights(const Ref<ImporterMesh> &p_mesh, int p_bind_count);

// Clothing skinned to a MetaHuman body but imported on its own (an FBX instanced under the DNA's
// Skeleton3D by hand) keeps its exporter's bone names, and Skeleton3D resolves skin binds by exact
// name: Unreal writes the root joint as `Root`, the DNA calls it `root`. The scene import option
// `metahuman/bind_to_dna` names the `.dna` the clothing is worn with; the skin binds are then matched
// to that skeleton exactly, else ignoring case (renaming the bind, the scene's bone and its
// animation tracks), an unmatched bind no vertex is weighted to is dropped, and an unmatched
// weighted one is reported by name.
class EditorScenePostImportPluginMetaHuman : public EditorScenePostImportPlugin {
	GDCLASS(EditorScenePostImportPluginMetaHuman, EditorScenePostImportPlugin);

public:
	virtual void get_import_options(const String &p_path, List<ResourceImporter::ImportOption> *r_options) override;
	virtual void pre_process(Node *p_scene, const HashMap<StringName, Variant> &p_options) override;
};
