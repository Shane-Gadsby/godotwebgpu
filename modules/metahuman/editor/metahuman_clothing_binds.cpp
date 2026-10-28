/**************************************************************************/
/*  metahuman_clothing_binds.cpp                                          */
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

#include "metahuman_clothing_binds.h"

#include "../metahuman_dna.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_uid.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/3d/skin.h"

LocalVector<int> metahuman_count_bind_weights(const Ref<ImporterMesh> &p_mesh, int p_bind_count) {
	LocalVector<int> counts;
	counts.resize_initialized(MAX(p_bind_count, 0));
	if (p_mesh.is_null()) {
		return counts;
	}
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		const Array arrays = p_mesh->get_surface_arrays(s);
		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		const PackedInt32Array bones = arrays[Mesh::ARRAY_BONES];
		const PackedFloat32Array weights = arrays[Mesh::ARRAY_WEIGHTS];
		if (vertices.is_empty() || bones.is_empty() || bones.size() != weights.size() || bones.size() % vertices.size() != 0) {
			continue;
		}
		const int per_vertex = bones.size() / vertices.size();
		for (int v = 0; v < vertices.size(); v++) {
			const int *vertex_bones = bones.ptr() + v * per_vertex;
			const float *vertex_weights = weights.ptr() + v * per_vertex;
			for (int k = 0; k < per_vertex; k++) {
				const int bind = vertex_bones[k];
				if (vertex_weights[k] <= 0.0f || bind < 0 || bind >= p_bind_count) {
					continue;
				}
				// One vertex counts once per bind, however many of its slots name it.
				bool counted = false;
				for (int j = 0; j < k && !counted; j++) {
					counted = vertex_bones[j] == bind && vertex_weights[j] > 0.0f;
				}
				if (!counted) {
					counts[bind]++;
				}
			}
		}
	}
	return counts;
}

namespace {

// The bone names of the skeleton the MetaHuman importer builds from p_dna_path: its joints, plus the
// body DNA's when it is a head DNA with a body beside it, which the importer merges in.
bool _dna_bone_names(const String &p_dna_path, HashSet<String> &r_names) {
	const String folder = p_dna_path.get_base_dir();
	String body_path = folder.path_join("body.dna");
	const String manifest_path = folder.path_join("CharacterAssemblyManifest.json");
	if (FileAccess::exists(manifest_path)) {
		const Variant manifest = JSON::parse_string(FileAccess::get_file_as_string(manifest_path));
		if (manifest.get_type() == Variant::DICTIONARY) {
			const Dictionary dna = Dictionary(manifest).get("dna", Dictionary());
			body_path = folder.path_join(String(dna.get("body", "body.dna")));
		}
	}
	Vector<String> paths;
	paths.push_back(p_dna_path);
	if (body_path.simplify_path() != p_dna_path.simplify_path() && FileAccess::exists(body_path)) {
		paths.push_back(body_path);
	}
	for (const String &path : paths) {
		const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
		ERR_FAIL_COND_V_MSG(bytes.is_empty(), false, vformat("MetaHuman: could not read '%s' (metahuman/bind_to_dna).", path));
		Ref<MetaHumanDNA> dna;
		dna.instantiate();
		dna->set_data(MetaHumanDNA::strip_geometry(bytes));
		ERR_FAIL_COND_V_MSG(!dna->is_valid(), false, vformat("MetaHuman: '%s' is not a DNA file RigLogic can read (metahuman/bind_to_dna).", path));
		for (int i = 0; i < dna->get_joint_count(); i++) {
			r_names.insert(dna->get_joint_name(i));
		}
	}
	return true;
}

void _collect_importer_meshes(Node *p_node, LocalVector<ImporterMeshInstance3D *> &r_list) {
	if (ImporterMeshInstance3D *mi = Object::cast_to<ImporterMeshInstance3D>(p_node)) {
		r_list.push_back(mi);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_importer_meshes(p_node->get_child(i), r_list);
	}
}

// Rewrites p_mesh's bone indices through p_remap (old bind -> new bind). ImporterMesh has no setter
// for a surface's arrays, so the surfaces are rebuilt with everything else they carry.
void _remap_bone_indices(const Ref<ImporterMesh> &p_mesh, const LocalVector<int> &p_remap) {
	struct SurfaceData {
		Mesh::PrimitiveType primitive;
		Array arrays;
		TypedArray<Array> blend_shapes;
		Dictionary lods;
		Ref<Material> material;
		String name;
		uint64_t flags;
	};
	LocalVector<SurfaceData> surfaces;
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		SurfaceData data;
		data.primitive = p_mesh->get_surface_primitive_type(s);
		data.arrays = p_mesh->get_surface_arrays(s).duplicate();
		for (int b = 0; b < p_mesh->get_blend_shape_count(); b++) {
			data.blend_shapes.push_back(p_mesh->get_surface_blend_shape_arrays(s, b));
		}
		for (int l = 0; l < p_mesh->get_surface_lod_count(s); l++) {
			data.lods[p_mesh->get_surface_lod_size(s, l)] = p_mesh->get_surface_lod_indices(s, l);
		}
		data.material = p_mesh->get_surface_material(s);
		data.name = p_mesh->get_surface_name(s);
		data.flags = p_mesh->get_surface_format(s);

		PackedInt32Array bones = data.arrays[Mesh::ARRAY_BONES];
		for (int i = 0; i < bones.size(); i++) {
			const int bind = bones[i];
			// A dropped bind has no weight on any vertex, so where it still appears (at weight 0) any
			// valid index does.
			bones.write[i] = (bind >= 0 && bind < (int)p_remap.size() && p_remap[bind] >= 0) ? p_remap[bind] : 0;
		}
		if (!bones.is_empty()) {
			data.arrays[Mesh::ARRAY_BONES] = bones;
		}
		surfaces.push_back(data);
	}

	Vector<String> blend_shape_names;
	for (int b = 0; b < p_mesh->get_blend_shape_count(); b++) {
		blend_shape_names.push_back(p_mesh->get_blend_shape_name(b));
	}
	const Mesh::BlendShapeMode blend_shape_mode = p_mesh->get_blend_shape_mode();
	p_mesh->clear();
	for (const String &name : blend_shape_names) {
		p_mesh->add_blend_shape(name);
	}
	p_mesh->set_blend_shape_mode(blend_shape_mode);
	for (const SurfaceData &data : surfaces) {
		p_mesh->add_surface(data.primitive, data.arrays, data.blend_shapes, data.lods, data.material, data.name, data.flags);
	}
}

void _rename_bone_tracks(Node *p_scene, Skeleton3D *p_skeleton, const HashMap<String, String> &p_renames) {
	TypedArray<Node> players = p_scene->find_children("*", "AnimationPlayer", true, false);
	if (Object::cast_to<AnimationPlayer>(p_scene)) {
		players.push_back(p_scene);
	}
	for (int p = 0; p < players.size(); p++) {
		AnimationPlayer *player = Object::cast_to<AnimationPlayer>(players[p]);
		Node *player_root = player ? player->get_node_or_null(player->get_root_node()) : nullptr;
		if (!player_root) {
			continue;
		}
		for (const StringName &name : player->get_sorted_animation_list()) {
			Ref<Animation> animation = player->get_animation(name);
			for (int i = 0; animation.is_valid() && i < animation->get_track_count(); i++) {
				const NodePath path = animation->track_get_path(i);
				if (path.get_subname_count() != 1) {
					continue;
				}
				const HashMap<String, String>::ConstIterator rename = p_renames.find(path.get_subname(0));
				if (!rename) {
					continue;
				}
				const String node_path = path.get_concatenated_names();
				if (player_root->get_node_or_null(NodePath(node_path)) == p_skeleton) {
					animation->track_set_path(i, NodePath(node_path + ":" + rename->value));
				}
			}
		}
	}
}

} // namespace

void EditorScenePostImportPluginMetaHuman::get_import_options(const String &p_path, List<ResourceImporter::ImportOption> *r_options) {
	if (p_path.get_extension().to_lower() == "dna") {
		return; // The MetaHuman importer binds its own clothing.
	}
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::STRING, "metahuman/bind_to_dna", PROPERTY_HINT_FILE_PATH, "*.dna"), String()));
}

void EditorScenePostImportPluginMetaHuman::pre_process(Node *p_scene, const HashMap<StringName, Variant> &p_options) {
	const HashMap<StringName, Variant>::ConstIterator option = p_options.find("metahuman/bind_to_dna");
	if (!option || String(option->value).is_empty() || !p_scene) {
		return;
	}
	const String dna_path = ResourceUID::ensure_path(option->value);
	HashSet<String> names;
	if (!_dna_bone_names(dna_path, names)) {
		return;
	}
	// Lowercase -> bone name. Names that differ only by case map to "", and match nothing loosely.
	HashMap<String, String> by_lower;
	for (const String &name : names) {
		const String lower = name.to_lower();
		HashMap<String, String>::Iterator existing = by_lower.find(lower);
		if (existing) {
			existing->value = String();
		} else {
			by_lower.insert(lower, name);
		}
	}

	LocalVector<ImporterMeshInstance3D *> instances;
	_collect_importer_meshes(p_scene, instances);
	HashMap<Ref<Skin>, LocalVector<ImporterMeshInstance3D *>> by_skin;
	HashMap<Ref<ImporterMesh>, Ref<Skin>> skin_of_mesh;
	// A mesh drawn with two different skins cannot have its bind indices rewritten for one of them.
	HashSet<Ref<ImporterMesh>> shared_meshes;
	for (ImporterMeshInstance3D *instance : instances) {
		const Ref<Skin> skin = instance->get_skin();
		const Ref<ImporterMesh> mesh = instance->get_mesh();
		if (skin.is_null() || mesh.is_null()) {
			continue;
		}
		by_skin[skin].push_back(instance);
		const HashMap<Ref<ImporterMesh>, Ref<Skin>>::ConstIterator known = skin_of_mesh.find(mesh);
		if (!known) {
			skin_of_mesh.insert(mesh, skin);
		} else if (known->value != skin) {
			shared_meshes.insert(mesh);
		}
	}

	HashMap<Skeleton3D *, HashMap<String, String>> bone_renames;
	int renamed = 0;
	int dropped = 0;
	for (const KeyValue<Ref<Skin>, LocalVector<ImporterMeshInstance3D *>> &E : by_skin) {
		const Ref<Skin> skin = E.key;
		ImporterMeshInstance3D *first = E.value[0];
		Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(first->get_node_or_null(first->get_skeleton_path()));
		const int bind_count = skin->get_bind_count();

		LocalVector<int> weighted;
		weighted.resize_initialized(bind_count);
		LocalVector<Ref<ImporterMesh>> meshes;
		bool can_drop = true;
		for (ImporterMeshInstance3D *instance : E.value) {
			const Ref<ImporterMesh> mesh = instance->get_mesh();
			if (meshes.has(mesh)) {
				continue;
			}
			meshes.push_back(mesh);
			can_drop = can_drop && !shared_meshes.has(mesh);
			const LocalVector<int> counts = metahuman_count_bind_weights(mesh, bind_count);
			for (int b = 0; b < bind_count; b++) {
				weighted[b] += counts[b];
			}
		}

		LocalVector<int> remap;
		remap.resize(bind_count);
		int kept = 0;
		for (int b = 0; b < bind_count; b++) {
			String name = skin->get_bind_name(b);
			if (name.is_empty() && skeleton && skin->get_bind_bone(b) >= 0 && skin->get_bind_bone(b) < skeleton->get_bone_count()) {
				// An index bind means nothing on another skeleton; name it after its bone.
				name = skeleton->get_bone_name(skin->get_bind_bone(b));
				skin->set_bind_name(b, name);
			}
			if (name.is_empty() || names.has(name)) {
				remap[b] = kept++;
				continue;
			}
			const HashMap<String, String>::ConstIterator loose = by_lower.find(name.to_lower());
			if (loose && !loose->value.is_empty()) {
				skin->set_bind_name(b, loose->value);
				// Rename the scene's own bone too, so the clothing still skins on the skeleton it came with.
				const int bone = skeleton ? skeleton->find_bone(name) : -1;
				if (bone >= 0 && skeleton->find_bone(loose->value) < 0) {
					skeleton->set_bone_name(bone, loose->value);
					bone_renames[skeleton][name] = loose->value;
				}
				print_verbose(vformat("MetaHuman: skin bind '%s' renamed '%s' to match '%s'.", name, loose->value, dna_path));
				renamed++;
				remap[b] = kept++;
				continue;
			}
			if (weighted[b] == 0 && can_drop) {
				print_verbose(vformat("MetaHuman: skin bind '%s' has no bone in '%s' and no weighted vertex; dropped.", name, dna_path));
				dropped++;
				remap[b] = -1;
				continue;
			}
			if (weighted[b] > 0) {
				WARN_PRINT(vformat("MetaHuman: the skeleton of '%s' has no bone '%s', which skin bind #%d of '%s' weights %d vertices to. Those vertices will follow the skeleton's first bone; rename the bone in the source file.",
						dna_path, name, b, first->get_name(), weighted[b]));
			}
			remap[b] = kept++;
		}

		if (kept < bind_count) {
			Ref<Skin> trimmed;
			trimmed.instantiate();
			trimmed->set_name(skin->get_name());
			for (int b = 0; b < bind_count; b++) {
				if (remap[b] < 0) {
					continue;
				}
				if (skin->get_bind_name(b) != StringName()) {
					trimmed->add_named_bind(skin->get_bind_name(b), skin->get_bind_pose(b));
				} else {
					trimmed->add_bind(skin->get_bind_bone(b), skin->get_bind_pose(b));
				}
			}
			for (ImporterMeshInstance3D *instance : E.value) {
				instance->set_skin(trimmed);
			}
			for (const Ref<ImporterMesh> &mesh : meshes) {
				_remap_bone_indices(mesh, remap);
			}
		}
	}

	for (const KeyValue<Skeleton3D *, HashMap<String, String>> &E : bone_renames) {
		_rename_bone_tracks(p_scene, E.key, E.value);
	}
	print_verbose(vformat("MetaHuman: skins matched to '%s': %d binds renamed, %d unweighted binds dropped.", dna_path, renamed, dropped));
}
