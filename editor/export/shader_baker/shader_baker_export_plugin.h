/**************************************************************************/
/*  shader_baker_export_plugin.h                                          */
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
#include "core/object/worker_thread_pool.h"
#include "core/os/condition_variable.h"
#include "core/templates/rb_set.h"
#include "editor/export/editor_export_plugin.h"
#include "servers/rendering/rendering_device_commons.h"

class ShaderRD;
class RenderingShaderContainerFormat;

// One batched conversion of compiled shader stages; see
// ShaderBakerExportPluginPlatform::create_stage_batch(). Driven from the main
// thread: add_stage() for every stage of every variant, start(), then poll
// is_done() while stepping a progress bar from get_done_count() (which counts
// up to get_total_count(), the number of stages the batch actually has to do
// itself), and finish() to join and publish. cancel() asks a running batch to
// stop early; finish() is still required afterwards.
class ShaderBakerStageBatch {
public:
	virtual void add_stage(const Vector<uint8_t> &p_spirv) = 0;
	virtual void start() = 0;
	virtual bool is_done() const = 0;
	virtual uint32_t get_done_count() const = 0;
	virtual uint32_t get_total_count() const = 0;
	virtual void cancel() = 0;
	virtual void finish() = 0;
	virtual ~ShaderBakerStageBatch() {}
};

class ShaderBakerExportPluginPlatform : public RefCounted {
	GDCLASS(ShaderBakerExportPluginPlatform, RefCounted);

public:
	virtual RenderingShaderContainerFormat *create_shader_container_format(const Ref<EditorExportPlatform> &p_platform, const Ref<EditorExportPreset> &p_preset) = 0;
	virtual bool matches_driver(const String &p_driver) = 0;

	// Capability answers the *export target's* device would give, for the
	// duration of the bake. Engine code builds its GLSL defines and picks its
	// shader groups from RenderingDevice::has_feature(), so without this the
	// baker compiles everything for the editor's device and the exported game
	// asks for variants that were never baked (webgpu_notes/TASKS.md Task 31).
	//
	// Default is empty, meaning "bake exactly as before". Only a platform that
	// knows its target disagrees with the editor needs to override this, so
	// existing bakers are unaffected.
	virtual void get_target_feature_overrides(HashMap<int, bool> &r_overrides) const {}

	// Optional: convert every compiled stage in one batched step before any
	// container is built from them, instead of inside each container's
	// set_code_from_spirv(). A platform whose container format does expensive
	// per-stage work there (WebGPU's WGSL bake shells out to a tool per stage)
	// returns true and hands back a batch; the plugin then holds every variant's
	// SPIR-V until all of them are compiled, runs the batch, and only then builds
	// the containers. Platforms that leave this alone bake exactly as before,
	// one container per task as its SPIR-V comes out of glslang.
	virtual bool uses_stage_batching() const { return false; }
	virtual ShaderBakerStageBatch *create_stage_batch() { return nullptr; }

	// Optional: ship the baked cache files in a pack of their own rather than
	// the main one, so a deploy that re-uploads whole files only re-uploads the
	// shaders when the shaders changed. The plugin then hands the files to the
	// export platform (ShaderBakerExportPlugin::take_separate_pack_files())
	// instead of add_file(); a platform that answers true has to write that pack
	// and arrange for the runtime to mount it. See platform/web/export.
	virtual bool uses_separate_pack(const Ref<EditorExportPreset> &p_preset) const { return false; }

	virtual ~ShaderBakerExportPluginPlatform() {}
};

class ShaderBakerExportPlugin : public EditorExportPlugin {
	GDSOFTCLASS(ShaderBakerExportPlugin, EditorExportPlugin);

protected:
	struct WorkItem {
		String cache_path;
		String shader_name;
		Vector<String> stage_sources;
		Vector<uint64_t> dynamic_buffers;
		int64_t variant = 0;
	};

	struct WorkResult {
		// Since this result is per group, this vector will have gaps in the data it covers as the indices must stay relative to all variants.
		Vector<PackedByteArray> variant_data;
	};

	struct ShaderGroupItem {
		String cache_path;
		LocalVector<int> variants;
		LocalVector<WorkerThreadPool::TaskID> variant_tasks;
	};

	// A variant whose stages have been compiled to SPIR-V but whose container
	// has not been built yet; only used when the platform batches stage baking.
	struct CompiledWorkItem {
		WorkItem item;
		Vector<RenderingDeviceCommons::ShaderStageSPIRVData> spirv_data;
	};

public:
	struct SeparatePackFile {
		String path;
		Vector<uint8_t> data;
	};

protected:

	String shader_cache_platform_name;
	String shader_cache_renderer_name;
	String shader_cache_export_path;
	RBSet<String> shader_paths_processed;
	HashMap<String, WorkResult> shader_work_results;
	Mutex shader_work_results_mutex;
	LocalVector<ShaderGroupItem> shader_group_items;
	RenderingShaderContainerFormat *shader_container_format = nullptr;
	String shader_container_driver;
	Vector<Ref<ShaderBakerExportPluginPlatform>> platforms;
	// The one matched by _initialize_container_format(); kept so the bake can ask
	// it for the target's capability answers (see get_target_feature_overrides()).
	Ref<ShaderBakerExportPluginPlatform> active_platform;
	uint64_t customization_configuration_hash = 0;
	// See ShaderBakerExportPluginPlatform::uses_stage_batching().
	bool batch_stage_baking = false;
	LocalVector<CompiledWorkItem> compiled_work_items;
	Mutex compiled_work_items_mutex;
	// See ShaderBakerExportPluginPlatform::uses_separate_pack().
	bool separate_pack = false;
	Vector<SeparatePackFile> separate_pack_files;
	uint32_t tasks_processed = 0;
	uint32_t tasks_total = 0;
	std::atomic<bool> tasks_cancelled;
	BinaryMutex tasks_mutex;
	ConditionVariable tasks_condition;

	virtual String get_name() const override;
	virtual bool _is_active(const Vector<String> &p_features) const;
	virtual bool _initialize_container_format(const Ref<EditorExportPlatform> &p_platform, const Ref<EditorExportPreset> &p_preset);
	virtual void _cleanup_container_format();
	virtual bool _initialize_cache_directory();
	virtual bool _begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override;
	virtual bool _begin_customize_scenes(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override;
	virtual void _end_customize_resources() override;
	virtual Ref<Resource> _customize_resource(const Ref<Resource> &p_resource, const String &p_path) override;
	virtual Node *_customize_scene(Node *p_root, const String &p_path) override;
	virtual uint64_t _get_customization_configuration_hash() const override;
	// p_origin is a short label for where this version came from ("embedded",
	// "embedded material", or a resource path), logged at verbose so a bake gap can
	// be diagnosed by comparing what the baker enumerated against what the runtime
	// asks for. See webgpu_notes/TASKS.md Task 32.
	virtual void _customize_shader_version(ShaderRD *p_shader, RID p_version, const String &p_origin = String());
	void _process_work_item(WorkItem p_work_item);
	// The second half of _process_work_item(): the container for one compiled
	// variant, stored into shader_work_results.
	void _build_container(const WorkItem &p_work_item, const Vector<RenderingDeviceCommons::ShaderStageSPIRVData> &p_spirv_data);
	void _build_compiled_work_item(uint32_t p_index, void *p_unused);
	// Runs the platform's stage batch over every compiled variant, then builds
	// their containers. Only called when batch_stage_baking is set.
	void _bake_compiled_work_items();

public:
	ShaderBakerExportPlugin();
	virtual ~ShaderBakerExportPlugin() override;
	void add_platform(Ref<ShaderBakerExportPluginPlatform> p_platform);
	void remove_platform(Ref<ShaderBakerExportPluginPlatform> p_platform);

	// The baked cache files of the export that just ran, when the platform
	// asked for them in a pack of their own (uses_separate_pack()); empty
	// otherwise. Each path is the res:// path the runtime looks the file up
	// at. Clears the list, so the platform takes them exactly once.
	Vector<SeparatePackFile> take_separate_pack_files();
};
