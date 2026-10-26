/**************************************************************************/
/*  shader_baker_export_plugin.cpp                                        */
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

#include "shader_baker_export_plugin.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access_compressed.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"
#include "core/version.h"
#include "editor/editor_node.h"
#include "scene/3d/label_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/sprite_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/material.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/rendering_shader_container.h"

// Ensure that AlphaCut is the same between the two classes so we can share the code to detect transparency.
static_assert(ENUM_MEMBERS_EQUAL(SpriteBase3D::ALPHA_CUT_DISABLED, Label3D::ALPHA_CUT_DISABLED));
static_assert(ENUM_MEMBERS_EQUAL(SpriteBase3D::ALPHA_CUT_DISCARD, Label3D::ALPHA_CUT_DISCARD));
static_assert(ENUM_MEMBERS_EQUAL(SpriteBase3D::ALPHA_CUT_OPAQUE_PREPASS, Label3D::ALPHA_CUT_OPAQUE_PREPASS));
static_assert(ENUM_MEMBERS_EQUAL(SpriteBase3D::ALPHA_CUT_HASH, Label3D::ALPHA_CUT_HASH));
static_assert(ENUM_MEMBERS_EQUAL(SpriteBase3D::ALPHA_CUT_MAX, Label3D::ALPHA_CUT_MAX));

String ShaderBakerExportPlugin::get_name() const {
	return "ShaderBaker";
}

bool ShaderBakerExportPlugin::_is_active(const Vector<String> &p_features) const {
	// Shader baker should only work when a RendererRD driver is active, as the embedded shaders won't be found otherwise.
	return RendererSceneRenderRD::get_singleton() != nullptr && RendererRD::MaterialStorage::get_singleton() != nullptr && p_features.has("shader_baker");
}

bool ShaderBakerExportPlugin::_initialize_container_format(const Ref<EditorExportPlatform> &p_platform, const Ref<EditorExportPreset> &p_preset) {
	shader_container_driver = p_preset->get_project_setting("rendering/rendering_device/driver");
	ERR_FAIL_COND_V_MSG(shader_container_driver.is_empty(), false, "Invalid `rendering/rendering_device/driver` setting, disabling shader baking.");

	for (Ref<ShaderBakerExportPluginPlatform> platform : platforms) {
		if (platform->matches_driver(shader_container_driver)) {
			shader_container_format = platform->create_shader_container_format(p_platform, p_preset);
			ERR_FAIL_NULL_V_MSG(shader_container_format, false, "Unable to create shader container format for the export platform.");
			active_platform = platform;
			return true;
		}
	}

	return false;
}

void ShaderBakerExportPlugin::_cleanup_container_format() {
	if (shader_container_format != nullptr) {
		memdelete(shader_container_format);
		shader_container_format = nullptr;
	}
}

bool ShaderBakerExportPlugin::_initialize_cache_directory() {
	shader_cache_export_path = get_export_base_path().path_join("shader_baker").path_join(shader_cache_platform_name).path_join(shader_container_driver);

	if (!DirAccess::dir_exists_absolute(shader_cache_export_path)) {
		Error err = DirAccess::make_dir_recursive_absolute(shader_cache_export_path);
		ERR_FAIL_COND_V_MSG(err != OK, false, "Can't create shader cache folder for exporting.");
	}

	return true;
}

bool ShaderBakerExportPlugin::_begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) {
	if (!_is_active(p_features)) {
		return false;
	}

	// The target capability override installed below applies inside this scope
	// only; each later baker callback opens its own. Between them the editor draws
	// frames with its own device's answers (RenderingDevice::ShaderBakeFeatureOverrideScope).
	RD::ShaderBakeFeatureOverrideScope override_scope;

	if (!_initialize_container_format(p_platform, get_export_preset())) {
		return false;
	}

	if (Engine::get_singleton()->is_generate_spirv_debug_info_enabled()) {
		WARN_PRINT("Shader baker can't generate a compatible shader when run with --generate-spirv-debug-info. Restart the editor without this argument if you want to bake shaders.");
		return false;
	}

	shader_cache_platform_name = p_platform->get_os_name();
	shader_cache_renderer_name = RendererSceneRenderRD::get_singleton()->get_name();
	tasks_processed = 0;
	tasks_total = 0;
	tasks_cancelled = false;
	batch_stage_baking = active_platform.is_valid() && active_platform->uses_stage_batching();
	separate_pack = active_platform.is_valid() && active_platform->uses_separate_pack(get_export_preset());
	compiled_work_items.clear();
	separate_pack_files.clear();
	extras_pack_files.clear();

	StringBuilder to_hash;
	to_hash.append("[GodotVersionNumber]");
	to_hash.append(GODOT_VERSION_NUMBER);
	to_hash.append("[GodotVersionHash]");
	to_hash.append(GODOT_VERSION_HASH);
	to_hash.append("[Renderer]");
	to_hash.append(shader_cache_renderer_name);
	customization_configuration_hash = to_hash.as_string().hash64();

	BitField<RenderingShaderLibrary::FeatureBits> renderer_features = {};
#ifndef XR_DISABLED
	bool xr_enabled = GLOBAL_GET("xr/shaders/enabled");
	renderer_features.set_flag(RenderingShaderLibrary::FEATURE_ADVANCED_BIT);
	if (xr_enabled) {
		renderer_features.set_flag(RenderingShaderLibrary::FEATURE_MULTIVIEW_BIT);
	}
#endif // XR_DISABLED

	int vrs_mode = GLOBAL_GET("rendering/vrs/mode");
	if (vrs_mode != 0) {
		renderer_features.set_flag(RenderingShaderLibrary::FEATURE_VRS_BIT);
	}

	// Both FP16 and FP32 variants should be included.
	renderer_features.set_flag(RenderingShaderLibrary::FEATURE_FP16_BIT);
	renderer_features.set_flag(RenderingShaderLibrary::FEATURE_FP32_BIT);

	RendererSceneRenderRD::get_singleton()->enable_features(renderer_features);

	// Answer capability queries as the export target's device would, for the rest
	// of the bake. Engine code builds its GLSL defines and picks its shader groups
	// from RenderingDevice::has_feature(), so without this every such shader is
	// baked in the editor's flavor and the exported game asks for one that was
	// never baked -- missing the shader cache entirely and recompiling from GLSL
	// on the main thread at load. See webgpu_notes/TASKS.md Task 31.
	//
	// Installed before any shader is enumerated, and paired with the clear in
	// _end_customize_resources(). Platforms that supply no overrides leave the
	// map empty and nothing below changes behavior for them.
	{
		HashMap<int, bool> target_feature_overrides;
		if (active_platform.is_valid()) {
			active_platform->get_target_feature_overrides(target_feature_overrides);
		}
		if (!target_feature_overrides.is_empty()) {
			RD::get_singleton()->shader_bake_feature_override_set(target_feature_overrides);
			// Subsystems whose defines were computed from the editor's answers at
			// startup recompute them now against the target's.
			ShaderRD::refresh_all_general_defines();
			print_verbose(vformat("Shader baker: baking with %d target capability override(s) instead of the editor's device answers.", target_feature_overrides.size()));
		}
	}

	// Which shader groups this export can never select, from the target's answers
	// and the project. Part of the customization hash: a different decision bakes
	// a different set of groups, so cached customizations must not be reused.
	_compute_unmet_group_requirements(get_export_preset());
	customization_configuration_hash = hash_murmur3_one_64(unmet_group_requirements, customization_configuration_hash);
	customization_configuration_hash = hash_murmur3_one_64(deferred_group_requirements, customization_configuration_hash);

	// Included all shaders created by renderers and effects.
	ShaderRD::shaders_embedded_set_lock();
	const ShaderRD::ShaderVersionPairSet &pair_set = ShaderRD::shaders_embedded_set_get();
	for (Pair<ShaderRD *, RID> pair : pair_set) {
		_customize_shader_version(pair.first, pair.second, "embedded shader");
	}

	ShaderRD::shaders_embedded_set_unlock();

	// Include all shaders created by embedded materials.
	RendererRD::MaterialStorage *material_storage = RendererRD::MaterialStorage::get_singleton();
	material_storage->shader_embedded_set_lock();
	const HashSet<RID> &rid_set = material_storage->shader_embedded_set_get();
	for (RID rid : rid_set) {
		RendererRD::MaterialStorage::ShaderData *shader_data = material_storage->shader_get_data(rid);
		if (shader_data != nullptr) {
			Pair<ShaderRD *, RID> shader_version_pair = shader_data->get_native_shader_and_version();
			if (shader_version_pair.first != nullptr) {
				_customize_shader_version(shader_version_pair.first, shader_version_pair.second, "embedded material");
			}
		}
	}

	material_storage->shader_embedded_set_unlock();

	// Finally, for every ShaderRD reached above, bake every version it currently
	// holds -- not just the ones an exported resource or scene led back to.
	//
	// Material shader versions are created with version_create(false), i.e. NOT
	// embedded, on the assumption that the resource and scene walks will find the
	// materials that matter. They do not always: a real export baked four
	// SceneForwardClustered versions while the game asked for a fifth (an unshaded,
	// fog-disabled variant of a textured BaseMaterial3D) that no walk reached, and
	// four separate attempts to find the owner by reasoning about where materials
	// come from were all wrong. The engine has already built every version it needs
	// by this point, so taking them all is both simpler and complete.
	//
	// Only when a target capability override is active, i.e. only for a platform
	// that opted in (currently WebGPU). It costs extra export-time work and bakes
	// some versions the game will never ask for, which is a trade worth making only
	// where an unbaked shader means a full GLSL->SPIR-V->WGSL compile on the main
	// thread at load. Other platforms keep exactly their previous behavior.
	if (RD::get_singleton()->shader_bake_feature_override_is_active()) {
		LocalVector<ShaderRD *> shaders_seen;
		ShaderRD::shaders_embedded_set_lock();
		for (Pair<ShaderRD *, RID> pair : ShaderRD::shaders_embedded_set_get()) {
			if (!shaders_seen.has(pair.first)) {
				shaders_seen.push_back(pair.first);
			}
		}
		ShaderRD::shaders_embedded_set_unlock();

		material_storage->shader_embedded_set_lock();
		for (RID rid : material_storage->shader_embedded_set_get()) {
			RendererRD::MaterialStorage::ShaderData *shader_data = material_storage->shader_get_data(rid);
			if (shader_data != nullptr) {
				Pair<ShaderRD *, RID> pair = shader_data->get_native_shader_and_version();
				if (pair.first != nullptr && !shaders_seen.has(pair.first)) {
					shaders_seen.push_back(pair.first);
				}
			}
		}
		material_storage->shader_embedded_set_unlock();

		for (ShaderRD *shader : shaders_seen) {
			for (RID version : shader->get_all_versions()) {
				// Re-visiting a version already queued above is free:
				// _customize_shader_version() skips any group whose cache path is
				// already in shader_paths_processed, so the sweep only adds versions
				// the earlier walks missed.
				_customize_shader_version(shader, version, "all-versions sweep");
			}
		}
	}

	return true;
}

bool ShaderBakerExportPlugin::_begin_customize_scenes(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) {
	if (!_is_active(p_features)) {
		return false;
	}

	if (shader_container_format == nullptr) {
		// Resource customization failed to initialize.
		return false;
	}

	return true;
}

void ShaderBakerExportPlugin::_end_customize_resources() {
	// Restore the editor's own capability answers and put the shader defines back
	// the way the running editor expects them; see the matching block in
	// _begin_customize_resources().
	if (RD::get_singleton()->shader_bake_feature_override_is_active()) {
		RD::get_singleton()->shader_bake_feature_override_clear();
		ShaderRD::refresh_all_general_defines();
	}

	if (!_initialize_cache_directory()) {
		return;
	}

	// Run a progress bar that waits for all shader baking tasks to finish.
	{
		bool progress_active = true;
		EditorProgress editor_progress("baking_shaders", TTR("Baking shaders"), tasks_total);
		editor_progress.step("Baking...", 0);
		while (progress_active) {
			uint32_t tasks_for_progress = 0;
			{
				MutexLock lock(tasks_mutex);
				if (tasks_processed >= tasks_total) {
					progress_active = false;
				} else {
					tasks_condition.wait(lock);
					tasks_for_progress = tasks_processed;
				}
			}

			if (progress_active && editor_progress.step("Baking...", tasks_for_progress)) {
				// User skipped the shader baker, we just don't pack the shaders in the project.
				tasks_cancelled = true;
				progress_active = false;
			}
		}
	}

	// Wait for every compilation task before reading any result. Waiting here
	// rather than group by group below changes nothing for a platform that
	// bakes per task, and is required for one that batches: its second phase
	// needs every variant's SPIR-V at once.
	for (const ShaderGroupItem &group_item : shader_group_items) {
		for (WorkerThreadPool::TaskID task_id : group_item.variant_tasks) {
			WorkerThreadPool::get_singleton()->wait_for_task_completion(task_id);
		}
	}

	if (batch_stage_baking && !tasks_cancelled) {
		_bake_compiled_work_items();
	}
	compiled_work_items.clear();

	// Either the main pack (add_file), the platform's separate shader pack, or
	// the lazily loaded extras.
	auto ship_file = [&](const String &p_path, const PackedByteArray &p_bytes, bool p_extras) {
		if (p_extras) {
			SeparatePackFile file;
			file.path = p_path;
			file.data = p_bytes;
			extras_pack_files.push_back(file);
		} else if (separate_pack) {
			SeparatePackFile file;
			file.path = p_path;
			file.data = p_bytes;
			separate_pack_files.push_back(file);
		} else {
			add_file(p_path, p_bytes, false);
		}
	};

	String shader_cache_user_dir = ShaderRD::get_shader_cache_user_dir();
	for (const ShaderGroupItem &group_item : shader_group_items) {
		if (!tasks_cancelled) {
			WorkResult work_result;
			{
				MutexLock lock(shader_work_results_mutex);
				work_result = shader_work_results[group_item.cache_path];
			}

			PackedByteArray cache_file_bytes = ShaderRD::save_shader_cache_bytes(group_item.variants, work_result.variant_data);
			ship_file(shader_cache_user_dir.path_join(group_item.cache_path), cache_file_bytes, group_item.extras);

			String cache_file_path = shader_cache_export_path.path_join(group_item.cache_path);
			if (!DirAccess::exists(cache_file_path)) {
				DirAccess::make_dir_recursive_absolute(cache_file_path.get_base_dir());
			}

			Ref<FileAccess> cache_file_access = FileAccess::open(cache_file_path, FileAccess::WRITE);
			if (cache_file_access.is_valid()) {
				cache_file_access->store_buffer(cache_file_bytes);
			}
		}
	}

	if (!tasks_cancelled) {
		String file_cache_path = shader_cache_export_path.path_join("file_cache");
		Ref<FileAccess> cache_list_access = FileAccess::open(file_cache_path, FileAccess::READ_WRITE);
		if (cache_list_access.is_null()) {
			cache_list_access = FileAccess::open(file_cache_path, FileAccess::WRITE);
		}

		if (cache_list_access.is_valid()) {
			String cache_list_line;
			while (cache_list_line = cache_list_access->get_line(), !cache_list_line.is_empty()) {
				// Only add if it wasn't already added, and not if this export pruned
				// its group: a previous export may have baked it.
				const String cache_group_dir = cache_list_line.get_slice("/", 1);
				if (!shader_paths_processed.has(cache_list_line) && !pruned_group_dirs.has(cache_group_dir)) {
					PackedByteArray cache_file_bytes = FileAccess::get_file_as_bytes(shader_cache_export_path.path_join(cache_list_line));
					if (!cache_file_bytes.is_empty()) {
						ship_file(shader_cache_user_dir.path_join(cache_list_line), cache_file_bytes, deferred_group_dirs.has(cache_group_dir));
					}
				}

				shader_paths_processed.erase(cache_list_line);
			}

			for (const String &shader_path : shader_paths_processed) {
				cache_list_access->store_line(shader_path);
			}

			cache_list_access->close();
		}
	}

	if (pruned_group_count > 0) {
		print_line(vformat("Shader baker: left out %d shader group(s) this export cannot use (shader_baker/variant_pruning).", pruned_group_count));
	}

	if (!extras_pack_files.is_empty()) {
		print_line(vformat("Shader baker: %d shader cache file(s) deferred to the shader extras.", extras_pack_files.size()));
	}

	shader_paths_processed.clear();
	pruned_group_dirs.clear();
	deferred_group_dirs.clear();
	pruned_group_count = 0;
	shader_work_results.clear();
	shader_group_items.clear();

	_cleanup_container_format();
}

Ref<Resource> ShaderBakerExportPlugin::_customize_resource(const Ref<Resource> &p_resource, const String &p_path) {
	RD::ShaderBakeFeatureOverrideScope override_scope;
	RendererRD::MaterialStorage *singleton = RendererRD::MaterialStorage::get_singleton();
	DEV_ASSERT(singleton != nullptr);

	Ref<Material> material = p_resource;
	if (material.is_valid()) {
		// BaseMaterial3D does not build its shader when the material is loaded: it
		// queues itself onto BaseMaterial3D::dirty_materials and waits for
		// flush_changes(), which is a SceneTree idle callback
		// (register_scene_types.cpp). No frame ticks during an export, so without
		// this a freshly-loaded material has no shader data yet and the lookup
		// below finds nothing to bake -- silently, since there is no error to
		// report. Static and idempotent: it drains every queued material at once,
		// so calling it per material is cheap after the first.
		// See webgpu_notes/TASKS.md Task 32.
		BaseMaterial3D::flush_changes();

		RID material_rid = material->get_rid();
		if (material_rid.is_valid()) {
			RendererRD::MaterialStorage::ShaderData *shader_data = singleton->material_get_shader_data(material_rid);
			if (shader_data != nullptr) {
				Pair<ShaderRD *, RID> shader_version_pair = shader_data->get_native_shader_and_version();
				if (shader_version_pair.first != nullptr) {
					_customize_shader_version(shader_version_pair.first, shader_version_pair.second, p_path.is_empty() ? String("material (no path)") : p_path);
				}
			}
		}
	}

	return Ref<Resource>();
}

Node *ShaderBakerExportPlugin::_customize_scene(Node *p_root, const String &p_path) {
	RD::ShaderBakeFeatureOverrideScope override_scope;
	LocalVector<Node *> nodes_to_visit;
	nodes_to_visit.push_back(p_root);
	while (!nodes_to_visit.is_empty()) {
		// Visit all nodes recursively in the scene to find the Label3Ds and Sprite3Ds.
		Node *node = nodes_to_visit[nodes_to_visit.size() - 1];
		nodes_to_visit.remove_at(nodes_to_visit.size() - 1);

		Label3D *label_3d = Object::cast_to<Label3D>(node);
		Sprite3D *sprite_3d = Object::cast_to<Sprite3D>(node);
		if (label_3d != nullptr || sprite_3d != nullptr) {
			// Create materials for Label3D and Sprite3D, which are normally generated at runtime on demand.
			HashMap<StringName, Variant> properties;

			// These must match the defaults set by Sprite3D/Label3D.
			properties["transparent"] = true; // Label3D doesn't have this property, but it is always true anyway.
			properties["shaded"] = false;
			properties["double_sided"] = true;
			properties["no_depth_test"] = false;
			properties["fixed_size"] = false;
			properties["billboard"] = StandardMaterial3D::BILLBOARD_DISABLED;
			properties["texture_filter"] = StandardMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS;
			properties["alpha_antialiasing_mode"] = StandardMaterial3D::ALPHA_ANTIALIASING_OFF;
			properties["alpha_cut"] = SpriteBase3D::ALPHA_CUT_DISABLED;

			List<PropertyInfo> property_list;
			node->get_property_list(&property_list);
			for (const PropertyInfo &info : property_list) {
				bool valid = false;
				Variant property = node->get(info.name, &valid);
				if (valid) {
					properties[info.name] = property;
				}
			}

			// This must follow the logic in Sprite3D::draw_texture_rect().
			BaseMaterial3D::Transparency mat_transparency = BaseMaterial3D::Transparency::TRANSPARENCY_DISABLED;
			if (properties["transparent"]) {
				SpriteBase3D::AlphaCutMode acm = SpriteBase3D::AlphaCutMode(int(properties["alpha_cut"]));
				if (acm == SpriteBase3D::ALPHA_CUT_DISCARD) {
					mat_transparency = BaseMaterial3D::Transparency::TRANSPARENCY_ALPHA_SCISSOR;
				} else if (acm == SpriteBase3D::ALPHA_CUT_OPAQUE_PREPASS) {
					mat_transparency = BaseMaterial3D::Transparency::TRANSPARENCY_ALPHA_DEPTH_PRE_PASS;
				} else if (acm == SpriteBase3D::ALPHA_CUT_HASH) {
					mat_transparency = BaseMaterial3D::Transparency::TRANSPARENCY_ALPHA_HASH;
				} else {
					mat_transparency = BaseMaterial3D::Transparency::TRANSPARENCY_ALPHA;
				}
			}

			StandardMaterial3D::BillboardMode billboard_mode = StandardMaterial3D::BillboardMode(int(properties["billboard"]));
			Ref<Material> sprite_3d_material = StandardMaterial3D::get_material_for_2d(bool(properties["shaded"]), mat_transparency, bool(properties["double_sided"]), billboard_mode == StandardMaterial3D::BILLBOARD_ENABLED, billboard_mode == StandardMaterial3D::BILLBOARD_FIXED_Y, false, bool(properties["no_depth_test"]), bool(properties["fixed_size"]), BaseMaterial3D::TextureFilter(int(properties["texture_filter"])), BaseMaterial3D::AlphaAntiAliasing(int(properties["alpha_antialiasing_mode"])));
			_customize_resource(sprite_3d_material, String());

			if (label_3d != nullptr) {
				// Generate variants with and without MSDF support since we don't have access to the font here.
				Ref<Material> label_3d_material = StandardMaterial3D::get_material_for_2d(bool(properties["shaded"]), mat_transparency, bool(properties["double_sided"]), billboard_mode == StandardMaterial3D::BILLBOARD_ENABLED, billboard_mode == StandardMaterial3D::BILLBOARD_FIXED_Y, true, bool(properties["no_depth_test"]), bool(properties["fixed_size"]), BaseMaterial3D::TextureFilter(int(properties["texture_filter"])), BaseMaterial3D::AlphaAntiAliasing(int(properties["alpha_antialiasing_mode"])));
				_customize_resource(label_3d_material, String());
			}
		}

		// Materials attached to geometry in the scene. Without this they are baked
		// only when they happen to be standalone resource files -- _customize_resource()
		// is not called for a material embedded as a sub-resource of a scene, and the
		// walk above only ever handled Label3D/Sprite3D. A model imported from glTF
		// keeps its materials inside the imported scene exactly that way, so its
		// shaders went unbaked and were recompiled from GLSL at load.
		//
		// The embedded-material snapshot in _begin_customize_resources() cannot cover
		// these either: it is taken before any scene is customized, so a material
		// shader created while loading one comes too late to be in it.
		//
		// _customize_resource() ignores a null Ref, so unset slots cost nothing.
		// See webgpu_notes/TASKS.md Task 31.
		GeometryInstance3D *geometry_instance = Object::cast_to<GeometryInstance3D>(node);
		if (geometry_instance != nullptr) {
			_customize_resource(geometry_instance->get_material_override(), String());
			_customize_resource(geometry_instance->get_material_overlay(), String());
		}

		MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(node);
		if (mesh_instance != nullptr) {
			Ref<Mesh> mesh = mesh_instance->get_mesh();
			if (mesh.is_valid()) {
				int surface_count = mesh->get_surface_count();
				for (int i = 0; i < surface_count; i++) {
					// Both slots matter: the mesh's own material is what ships with the
					// asset, the override is what the scene set on top of it, and either
					// can be the one actually drawn.
					_customize_resource(mesh->surface_get_material(i), String());
					_customize_resource(mesh_instance->get_surface_override_material(i), String());
				}
			}
		}

		// Visit children.
		int child_count = node->get_child_count();
		for (int i = 0; i < child_count; i++) {
			nodes_to_visit.push_back(node->get_child(i));
		}
	}

	return nullptr;
}

// Variant pruning (webgpu_notes/TASKS.md §2.27). Forward+'s advanced scene
// shader group -- motion vectors, separate specular, lightmaps, VoxelGI and SDF
// depth passes -- is about 70% of every material's baked shader bytes, and
// render_forward_clustered.cpp enables it only when a frame needs one of:
//
//   motion vectors      TAA, FSR 2 / MetalFX temporal upscaling, a CompositorEffect
//                       with needs_motion_vectors, the motion-vector debug draw
//   separate specular   subsurface scattering, a CompositorEffect with
//                       needs_separate_specular
//   lightmaps           a LightmapGI
//   VoxelGI / SDFGI     a VoxelGI node (also the VoxelGI dynamic-object render),
//                       an Environment with SDFGI
//
// The scan below looks for all of them, conservatively: anything that *mentions*
// one keeps the group. Property names are matched as bytes, which works for text
// resources, scripts and shaders, and for binary resources too, whose property
// names sit in a string table. Neither saver stores a property at its default, so
// in a resource the name alone means it was set. Scripts and comments that only
// talk about a feature keep the group too, which costs bytes, not correctness.
//
// Getting this wrong is not fatal: the runtime finds no cache file for the group
// and compiles it from source the moment the feature turns on, as an unbaked
// export would (ShaderRD::_load_from_cache() says so in a warning).
struct ShaderBakerAdvancedMarker {
	const char *text;
	const char *reason;
};

static const ShaderBakerAdvancedMarker shader_baker_advanced_markers[] = {
	{ "sdfgi_enabled", "SDFGI (Environment.sdfgi_enabled)" },
	{ "use_taa", "TAA (use_taa)" },
	{ "FSR2", "FSR 2 upscaling" },
	{ "METALFX_TEMPORAL", "MetalFX temporal upscaling" },
	{ "needs_motion_vectors", "a CompositorEffect needing motion vectors" },
	{ "needs_separate_specular", "a CompositorEffect needing separate specular" },
	{ "DEBUG_DRAW_MOTION_VECTORS", "the motion vector debug draw" },
	{ "LightmapGI", "LightmapGI" },
	{ "VoxelGI", "VoxelGI" },
	{ "subsurf_scatter_enabled", "subsurface scattering (BaseMaterial3D)" },
	{ "SSS_STRENGTH", "subsurface scattering (shader SSS_STRENGTH)" },
	{ "bake_render_uv2", "RenderingServer.bake_render_uv2()" },
	// The same features through RenderingServer directly.
	{ "environment_set_sdfgi", "SDFGI (RenderingServer.environment_set_sdfgi())" },
	{ "voxel_gi_create", "VoxelGI (RenderingServer.voxel_gi_create())" },
	{ "lightmap_create", "lightmaps (RenderingServer.lightmap_create())" },
	{ "NEEDS_MOTION_VECTORS", "a compositor effect needing motion vectors" },
	{ "NEEDS_SEPARATE_SPECULAR", "a compositor effect needing separate specular" },
};

static bool _shader_baker_bytes_contain(const Vector<uint8_t> &p_bytes, const char *p_needle) {
	const int64_t needle_len = strlen(p_needle);
	const int64_t size = p_bytes.size();
	const uint8_t *data = p_bytes.ptr();
	for (int64_t i = 0; i + needle_len <= size; i++) {
		const void *found = memchr(data + i, p_needle[0], size - needle_len - i + 1);
		if (found == nullptr) {
			return false;
		}
		i = (const uint8_t *)found - data;
		if (memcmp(data + i, p_needle, needle_len) == 0) {
			return true;
		}
	}
	return false;
}

static Vector<uint8_t> _shader_baker_read_for_scan(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return Vector<uint8_t>();
	}
	uint8_t header[4] = {};
	f->get_buffer(header, 4);
	if (header[0] == 'R' && header[1] == 'S' && header[2] == 'C' && header[3] == 'C') {
		// A compressed binary resource; same unwrapping ResourceLoaderBinary does.
		Ref<FileAccessCompressed> fac;
		fac.instantiate();
		if (fac->open_after_magic(f) != OK) {
			return Vector<uint8_t>();
		}
		Vector<uint8_t> bytes;
		bytes.resize(fac->get_length());
		fac->get_buffer(bytes.ptrw(), bytes.size());
		return bytes;
	}
	f->seek(0);
	Vector<uint8_t> bytes;
	bytes.resize(f->get_length());
	f->get_buffer(bytes.ptrw(), bytes.size());
	return bytes;
}

// Returns the first reason found to keep the advanced group, or an empty string.
static String _shader_baker_scan_dir_for_advanced(const String &p_dir) {
	static const char *scanned_extensions[] = { "tscn", "tres", "scn", "res", "material", "lmbake", "gdshader", "gdshaderinc", "gd", "gdb", "cs" };

	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return String();
	}
	if (p_dir != "res://" && da->file_exists(".gdignore")) {
		return String();
	}

	LocalVector<String> subdirs;
	da->list_dir_begin();
	for (String name = da->get_next(); !name.is_empty(); name = da->get_next()) {
		if (name.begins_with(".")) {
			continue; // ".", "..", ".godot", hidden folders.
		}
		const String path = p_dir.path_join(name);
		if (da->current_is_dir()) {
			subdirs.push_back(path);
			continue;
		}
		const String ext = name.get_extension().to_lower();
		bool scanned = false;
		for (const char *e : scanned_extensions) {
			if (ext == e) {
				scanned = true;
				break;
			}
		}
		if (!scanned) {
			continue;
		}

		const Vector<uint8_t> bytes = _shader_baker_read_for_scan(path);
		for (const ShaderBakerAdvancedMarker &marker : shader_baker_advanced_markers) {
			if (_shader_baker_bytes_contain(bytes, marker.text)) {
				return vformat("%s, in %s", marker.reason, path);
			}
		}
		// scaling_3d_mode is set to a spatial scaler as often as to a temporal one.
		// Text resources say which (2 is FSR 2, 4 MetalFX temporal; see
		// RSE::ViewportScaling3DMode); anything else that sets it keeps the group.
		if (_shader_baker_bytes_contain(bytes, "scaling_3d_mode")) {
			const bool text_resource = ext == "tscn" || ext == "tres";
			if (!text_resource || _shader_baker_bytes_contain(bytes, "scaling_3d_mode = 2") || _shader_baker_bytes_contain(bytes, "scaling_3d_mode = 4")) {
				return vformat("a temporal 3D scaling mode (scaling_3d_mode), in %s", path);
			}
		}
	}
	da->list_dir_end();

	for (const String &subdir : subdirs) {
		const String reason = _shader_baker_scan_dir_for_advanced(subdir);
		if (!reason.is_empty()) {
			return reason;
		}
	}
	return String();
}

void ShaderBakerExportPlugin::_compute_unmet_group_requirements(const Ref<EditorExportPreset> &p_preset) {
	unmet_group_requirements = 0;
	deferred_group_requirements = 0;
	pruned_group_dirs.clear();
	deferred_group_dirs.clear();
	pruned_group_count = 0;

	// Only with a capability override: without one the bake already covers just
	// the groups the editor has enabled, and the target answers are the editor's.
	if (!RD::get_singleton()->shader_bake_feature_override_is_active() || active_platform.is_null() || !active_platform->uses_variant_pruning(p_preset)) {
		return;
	}

	// Called with the target's capability override installed, so these are the
	// target device's answers.
	RenderingDevice *rd = RD::get_singleton();
	unmet_group_requirements |= rd->has_feature(RD::SUPPORTS_HALF_FLOAT) ? ShaderRD::GROUP_BAKE_REQUIRES_NO_HALF_FLOAT : ShaderRD::GROUP_BAKE_REQUIRES_HALF_FLOAT;
	unmet_group_requirements |= rd->has_feature(RD::SUPPORTS_IMAGE_ATOMIC_32_BIT) ? ShaderRD::GROUP_BAKE_REQUIRES_NO_IMAGE_ATOMICS : ShaderRD::GROUP_BAKE_REQUIRES_IMAGE_ATOMICS;
	unmet_group_requirements |= rd->has_feature(RD::SUPPORTS_VULKAN_MEMORY_MODEL) ? ShaderRD::GROUP_BAKE_REQUIRES_NO_VULKAN_MEMORY_MODEL : ShaderRD::GROUP_BAKE_REQUIRES_VULKAN_MEMORY_MODEL;
	if (!rd->has_feature(RD::SUPPORTS_ATTACHMENT_VRS)) {
		unmet_group_requirements |= ShaderRD::GROUP_BAKE_REQUIRES_VRS;
	}

	if (!bool(p_preset->get_project_setting("xr/shaders/enabled"))) {
		unmet_group_requirements |= ShaderRD::GROUP_BAKE_REQUIRES_XR;
	}

	String advanced_reason;
	if (bool(p_preset->get_project_setting("rendering/anti_aliasing/quality/use_taa"))) {
		advanced_reason = "TAA (project setting rendering/anti_aliasing/quality/use_taa)";
	} else {
		const RSE::ViewportScaling3DMode scaling_mode = RSE::ViewportScaling3DMode(int(p_preset->get_project_setting("rendering/scaling_3d/mode")));
		if (RSE::scaling_3d_mode_type(scaling_mode) == RSE::VIEWPORT_SCALING_3D_TYPE_TEMPORAL) {
			advanced_reason = "a temporal 3D scaling mode (project setting rendering/scaling_3d/mode)";
		} else {
			advanced_reason = _shader_baker_scan_dir_for_advanced("res://");
			if (advanced_reason.is_empty()) {
				// The scan above skips hidden folders, so it never sees what importers produce: a
				// glTF, FBX or MetaHuman scene keeps its materials in its imported .scn.
				advanced_reason = _shader_baker_scan_dir_for_advanced(ProjectSettings::get_singleton()->get_imported_files_path());
			}
		}
	}

	if (advanced_reason.is_empty()) {
		if (active_platform->uses_lazy_shader_extras(p_preset)) {
			deferred_group_requirements |= ShaderRD::GROUP_BAKE_REQUIRES_ADVANCED;
			print_line("Shader baker: the project uses none of TAA, FSR 2, LightmapGI, VoxelGI, SDFGI, subsurface scattering or motion-vector compositor effects, so Forward+'s advanced shader variants go to the shader extras, which load in the background after the game starts.");
		} else {
			unmet_group_requirements |= ShaderRD::GROUP_BAKE_REQUIRES_ADVANCED;
			print_line("Shader baker: the project uses none of TAA, FSR 2, LightmapGI, VoxelGI, SDFGI, subsurface scattering or motion-vector compositor effects, so Forward+'s advanced shader variants are not baked. If the game turns one of them on in a way the export cannot see (GDExtension code, say), they compile when first used; set shader_baker/variant_pruning to Disabled to bake them.");
		}
	} else {
		print_line(vformat("Shader baker: baking Forward+'s advanced shader variants, needed for %s.", advanced_reason));
	}
}

uint64_t ShaderBakerExportPlugin::_get_customization_configuration_hash() const {
	return customization_configuration_hash;
}

void ShaderBakerExportPlugin::_customize_shader_version(ShaderRD *p_shader, RID p_version, const String &p_origin) {
	// Names every version the baker actually enumerates. A missing shader at
	// runtime prints "Shader cache miss for <name>/<group>/<sha1>" (ShaderRD::
	// _load_from_cache), so the two logs together say whether a version was never
	// enumerated or was enumerated under a different key -- which is the
	// distinction that matters and is invisible from either side alone.
	if (OS::get_singleton()->is_stdout_verbose()) {
		print_verbose(vformat("Shader baker: baking '%s' from %s",
				p_shader->version_get_cache_file_relative_path(p_version, 0, shader_container_driver),
				p_origin.is_empty() ? String("<unknown>") : p_origin));
		print_verbose(vformat("  ^ version is: %s", p_shader->version_get_debug_fingerprint(p_version)));
	}

	const int64_t variant_count = p_shader->get_variant_count();
	const int64_t group_count = p_shader->get_group_count();
	LocalVector<ShaderGroupItem> group_items;
	group_items.resize(group_count);

	// With a target capability override active, an enabled *group* reflects a
	// choice the *editor's* device made and the target's may differ -- fog.cpp
	// picks its shader group straight from has_feature() via
	// _get_fog_shader_group(), so on WebGPU the runtime asks for the no-atomics
	// group the Vulkan editor never enabled. Baking every group costs some extra
	// export-time work and removes a whole class of "the target wanted a group the
	// editor never turned on". Without an override this is exactly the previous
	// behavior.
	//
	// Groups only, never variants. In the VariantDefine path every variant starts
	// enabled and `default_enabled` gates the *group*, so a disabled variant is
	// never a capability inference -- it is always an explicit set_variant_enabled(
	// ..., false) meaning "do not build this one" (vrs.cpp's XR-off multiview
	// variants, scene_shader_forward_mobile's FP16/FP32). Baking those anyway
	// produced 26 "Unable to retrieve SPIR-V data for shader" errors on a real
	// export.
	const bool bake_all_groups = RD::get_singleton()->shader_bake_feature_override_is_active();

	RBSet<uint32_t> groups_to_compile;
	for (int64_t i = 0; i < group_count; i++) {
		if (!bake_all_groups && !p_shader->is_group_enabled(i)) {
			continue;
		}

		if (p_shader->get_group_bake_requirements(i) & unmet_group_requirements) {
			const String group_dir = p_shader->get_group_cache_dir_name(i);
			if (!group_dir.is_empty() && !pruned_group_dirs.has(group_dir)) {
				pruned_group_dirs.insert(group_dir);
				print_verbose(vformat("Shader baker: not baking %s group %d (requirements 0x%x not met by this export).", p_shader->get_name(), i, p_shader->get_group_bake_requirements(i)));
			}
			pruned_group_count++;
			continue;
		}

		String cache_path = p_shader->version_get_cache_file_relative_path(p_version, i, shader_container_driver);
		if (shader_paths_processed.has(cache_path)) {
			continue;
		}

		shader_paths_processed.insert(cache_path);
		groups_to_compile.insert(i);

		group_items[i].extras = (p_shader->get_group_bake_requirements(i) & deferred_group_requirements) != 0;
		if (group_items[i].extras) {
			deferred_group_dirs.insert(p_shader->get_group_cache_dir_name(i));
		}
		group_items[i].cache_path = cache_path;
		group_items[i].variants = p_shader->get_group_to_variants(i);

		{
			MutexLock lock(shader_work_results_mutex);
			shader_work_results[cache_path].variant_data.resize(variant_count);
		}
	}

	for (int64_t i = 0; i < variant_count; i++) {
		int group = p_shader->get_variant_to_group(i);
		if (!p_shader->is_variant_enabled(i) || !groups_to_compile.has(group)) {
			continue;
		}

		WorkItem work_item;
		work_item.cache_path = group_items[group].cache_path;
		work_item.shader_name = p_shader->get_name();
		work_item.stage_sources = p_shader->version_build_variant_stage_sources(p_version, i);
		work_item.dynamic_buffers = p_shader->get_dynamic_buffers();
		work_item.variant = i;

		WorkerThreadPool::TaskID task_id = WorkerThreadPool::get_singleton()->add_template_task(this, &ShaderBakerExportPlugin::_process_work_item, work_item);
		group_items[group].variant_tasks.push_back(task_id);
		tasks_total++;
	}

	for (uint32_t i : groups_to_compile) {
		shader_group_items.push_back(group_items[i]);
	}
}

void ShaderBakerExportPlugin::_process_work_item(WorkItem p_work_item) {
	if (!tasks_cancelled) {
		// Only process the item if the tasks haven't been cancelled by the user yet.
		Vector<RD::ShaderStageSPIRVData> spirv_data = ShaderRD::compile_stages(p_work_item.stage_sources, p_work_item.dynamic_buffers, p_work_item.shader_name);
		if (unlikely(spirv_data.is_empty())) {
			ERR_PRINT("Unable to retrieve SPIR-V data for shader.");
		} else if (batch_stage_baking) {
			// The container is built in _bake_compiled_work_items(), after the
			// platform has converted every variant's stages in one go.
			CompiledWorkItem compiled;
			compiled.item = p_work_item;
			compiled.spirv_data = spirv_data;
			MutexLock lock(compiled_work_items_mutex);
			compiled_work_items.push_back(compiled);
		} else {
			_build_container(p_work_item, spirv_data);
		}
	}

	{
		MutexLock lock(tasks_mutex);
		tasks_processed++;
	}

	tasks_condition.notify_one();
}

void ShaderBakerExportPlugin::_build_container(const WorkItem &p_work_item, const Vector<RD::ShaderStageSPIRVData> &p_spirv_data) {
	Ref<RenderingShaderContainer> shader_container = shader_container_format->create_container();

	// Compile shader binary from SPIR-V.
	bool code_compiled = shader_container->set_code_from_spirv(p_work_item.shader_name, p_spirv_data);
	if (unlikely(!code_compiled)) {
		ERR_PRINT("Failed to compile code to native for SPIR-V.");
		return;
	}

	PackedByteArray shader_bytes = shader_container->to_bytes();
	MutexLock lock(shader_work_results_mutex);
	shader_work_results[p_work_item.cache_path].variant_data.ptrw()[p_work_item.variant] = shader_bytes;
}

void ShaderBakerExportPlugin::_build_compiled_work_item(uint32_t p_index, void *p_unused) {
	if (tasks_cancelled) {
		return;
	}
	const CompiledWorkItem &compiled = compiled_work_items[p_index];
	_build_container(compiled.item, compiled.spirv_data);
}

void ShaderBakerExportPlugin::_bake_compiled_work_items() {
	if (compiled_work_items.is_empty()) {
		return;
	}

	ShaderBakerStageBatch *batch = active_platform->create_stage_batch();
	ERR_FAIL_NULL_MSG(batch, "Shader baker platform asked for batched stage baking but returned no batch.");

	for (const CompiledWorkItem &compiled : compiled_work_items) {
		for (const RD::ShaderStageSPIRVData &stage : compiled.spirv_data) {
			batch->add_stage(stage.spirv);
		}
	}
	batch->start();

	// The batch runs on its own threads; this thread owns the progress bar.
	// Polling at ~60 Hz keeps the bar live without costing the batch anything.
	if (batch->get_total_count() > 0) {
		EditorProgress batch_progress("baking_shader_stages", TTR("Converting shaders"), batch->get_total_count(), true);
		while (!batch->is_done()) {
			if (batch_progress.step(TTR("Converting..."), batch->get_done_count())) {
				batch->cancel();
				tasks_cancelled = true;
				break;
			}
			OS::get_singleton()->delay_usec(16000);
		}
	}
	batch->finish();
	memdelete(batch);

	if (tasks_cancelled) {
		return;
	}

	// Every stage's result is published; building the containers is now the
	// cheap part (reflection and serialization), spread across the pool.
	EditorProgress container_progress("baking_shader_containers", TTR("Packing shaders"), (int)compiled_work_items.size());
	WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_template_group_task(this, &ShaderBakerExportPlugin::_build_compiled_work_item, (void *)nullptr, (int)compiled_work_items.size(), -1, false, "Shader baker: build containers");
	while (!WorkerThreadPool::get_singleton()->is_group_task_completed(group)) {
		container_progress.step(TTR("Packing..."), (int)WorkerThreadPool::get_singleton()->get_group_processed_element_count(group));
		OS::get_singleton()->delay_usec(16000);
	}
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);
}

Vector<ShaderBakerExportPlugin::SeparatePackFile> ShaderBakerExportPlugin::take_extras_pack_files() {
	Vector<SeparatePackFile> files = extras_pack_files;
	extras_pack_files.clear();
	return files;
}

Vector<ShaderBakerExportPlugin::SeparatePackFile> ShaderBakerExportPlugin::take_separate_pack_files() {
	Vector<SeparatePackFile> files = separate_pack_files;
	separate_pack_files.clear();
	return files;
}

ShaderBakerExportPlugin::ShaderBakerExportPlugin() {
	// Do nothing.
}

ShaderBakerExportPlugin::~ShaderBakerExportPlugin() {
	// Do nothing.
}

void ShaderBakerExportPlugin::add_platform(Ref<ShaderBakerExportPluginPlatform> p_platform) {
	platforms.push_back(p_platform);
}

void ShaderBakerExportPlugin::remove_platform(Ref<ShaderBakerExportPluginPlatform> p_platform) {
	platforms.erase(p_platform);
}
