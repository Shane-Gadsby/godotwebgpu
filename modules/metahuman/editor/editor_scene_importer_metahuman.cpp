/**************************************************************************/
/*  editor_scene_importer_metahuman.cpp                                   */
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

#include "editor_scene_importer_metahuman.h"

#include "../metahuman_dna.h"
#include "../metahuman_hair_simulator.h"
#include "../metahuman_rig.h"
#include "../metahuman_riglogic.h"
#include "metahuman_groom.h"
#include "metahuman_hair.h"
#include "metahuman_shaders.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/3d/skin.h"
#include "scene/resources/material.h"
#include "scene/resources/surface_tool.h"

#include "modules/modules_enabled.gen.h" // For fbx.

#include <iterator>

#ifdef MODULE_FBX_ENABLED
#include "modules/fbx/fbx_document.h"
#include "modules/fbx/fbx_state.h"
#endif

namespace {

// Marks the texture import settings this importer chose, so it applies them once and never
// overwrites what a user changed afterwards.
const String TEXTURE_GENERATOR_KEY = "metahuman_importer";

// Default LOD switch distances in meters, scaled by the `lods/distance_scale` option. The face
// changes LOD sooner than the body: its LOD 0 alone is 840 joints and 24 k vertices.
const float FACE_LOD_DISTANCES[] = { 1.5, 3.0, 5.0, 8.0, 12.0, 18.0, 26.0 };
const float BODY_LOD_DISTANCES[] = { 3.0, 8.0, 16.0 };

String _dna_string(const dna::StringView &p_view) {
	return String::utf8(p_view.data(), p_view.size());
}

struct LoadedDNA {
	rl4::ScopedPtr<rl4::MemoryStream> stream;
	rl4::ScopedPtr<rl4::BinaryStreamReader> reader;
	Ref<MetaHumanDNA> runtime;
	// DNA joint index -> skeleton bone index.
	Vector<int> joint_bones;
};

bool _load_dna(const String &p_path, LoadedDNA &r_dna) {
	const PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path);
	ERR_FAIL_COND_V_MSG(bytes.is_empty(), false, vformat("MetaHuman: could not read '%s'.", p_path));

	r_dna.stream = rl4::makeScoped<rl4::MemoryStream>(bytes.size());
	r_dna.stream->write(reinterpret_cast<const char *>(bytes.ptr()), bytes.size());
	r_dna.stream->seek(0);
	r_dna.reader = rl4::makeScoped<rl4::BinaryStreamReader>(r_dna.stream.get());
	r_dna.reader->read();
	ERR_FAIL_COND_V_MSG(!rl4::Status::isOk(), false, vformat("MetaHuman: could not read the DNA in '%s': %s", p_path, rl4::Status::get().message));

	r_dna.runtime.instantiate();
	r_dna.runtime->set_data(MetaHumanDNA::strip_geometry(bytes));
	ERR_FAIL_COND_V_MSG(!r_dna.runtime->is_valid(), false, vformat("MetaHuman: RigLogic could not load the rig in '%s'.", p_path));
	return true;
}

Variant _load_json(const String &p_path) {
	if (!FileAccess::exists(p_path)) {
		return Variant();
	}
	const String text = FileAccess::get_file_as_string(p_path);
	JSON json;
	if (json.parse(text) != OK) {
		ERR_PRINT(vformat("MetaHuman: '%s' is not valid JSON (line %d: %s).", p_path, json.get_error_line(), json.get_error_message()));
		return Variant();
	}
	return json.get_data();
}

struct Context {
	String folder;
	HashMap<StringName, Variant> options;
	uint32_t flags = 0;

	Dictionary manifest;
	HashMap<String, Dictionary> material_defs; // By material id.
	HashMap<String, String> material_by_slot; // Material id by slot (`head_shader_shader`, ...).
	HashMap<String, Ref<Material>> materials; // By material id.
	HashMap<String, Ref<Shader>> shaders; // By kind.
	HashMap<String, Ref<Texture2D>> textures; // By path.

	Skeleton3D *skeleton = nullptr;
	Ref<Skin> skin;
	Vector<Transform3D> global_rests;

	// Head LOD 0, which groom roots bind to.
	MetaHumanSkinSurface head_surface;
	// The groom eyelashes replace the DNA's eyelash cards.
	bool groom_eyelashes = false;
	// Head LOD 0 positions, for fitting the hair colliders.
	LocalVector<Vector3> head_vertices;
	// Modifiers to add under the skeleton after MetaHumanRig.
	LocalVector<Node *> pending_modifiers;

	template <typename T>
	T option(const char *p_name, const T &p_default) const {
		const HashMap<StringName, Variant>::ConstIterator E = options.find(StringName(p_name));
		return E ? T(E->value) : p_default;
	}
};

// Skeleton --------------------------------------------------------------------------------------

void _add_joints(Context &p_ctx, LoadedDNA &p_dna) {
	const Vector<MetaHumanDNA::Joint> &joints = p_dna.runtime->get_joints();
	p_dna.joint_bones.resize(joints.size());
	for (int i = 0; i < joints.size(); i++) {
		const MetaHumanDNA::Joint &joint = joints[i];
		int bone = p_ctx.skeleton->find_bone(joint.name);
		if (bone < 0) {
			bone = p_ctx.skeleton->add_bone(joint.name);
			if (joint.parent >= 0) {
				// Parents come before children in DNA joint order.
				p_ctx.skeleton->set_bone_parent(bone, p_dna.joint_bones[joint.parent]);
			}
			p_ctx.skeleton->set_bone_rest(bone, Transform3D(Basis(joint.rotation).scaled_local(joint.scale), joint.translation));
			p_ctx.skeleton->set_bone_pose(bone, p_ctx.skeleton->get_bone_rest(bone));
		}
		// A joint already in the skeleton keeps the transform it was added with. That is the body's:
		// the head DNA stores its root (`spine_04`) in world space, and its other shared joints
		// match the body's.
		p_dna.joint_bones.write[i] = bone;
	}
}

void _finish_skeleton(Context &p_ctx) {
	const int bone_count = p_ctx.skeleton->get_bone_count();
	p_ctx.global_rests.resize(bone_count);
	for (int i = 0; i < bone_count; i++) {
		const int parent = p_ctx.skeleton->get_bone_parent(i);
		// Bones are added parent first, so the parent's global rest is already known.
		const Transform3D rest = p_ctx.skeleton->get_bone_rest(i);
		p_ctx.global_rests.write[i] = parent >= 0 ? p_ctx.global_rests[parent] * rest : rest;
	}
	// One skin for every mesh: the skeleton then updates a single render-server skeleton per frame,
	// however many LODs and clothes it drives. Bind index = bone index.
	p_ctx.skin.instantiate();
	for (int i = 0; i < bone_count; i++) {
		p_ctx.skin->add_named_bind(p_ctx.skeleton->get_bone_name(i), p_ctx.global_rests[i].affine_inverse());
	}
}

// Textures and materials ------------------------------------------------------------------------

enum TextureKind {
	TEXTURE_COLOR,
	TEXTURE_DATA,
	TEXTURE_NORMAL,
};

Ref<Texture2D> _texture(Context &p_ctx, const String &p_relative_path, TextureKind p_kind) {
	const String path = p_ctx.folder.path_join(p_relative_path);
	if (const Ref<Texture2D> *existing = p_ctx.textures.getptr(path)) {
		return *existing;
	}
	if (!FileAccess::exists(path)) {
		WARN_PRINT(vformat("MetaHuman: texture '%s' is missing.", path));
		p_ctx.textures.insert(path, Ref<Texture2D>());
		return Ref<Texture2D>();
	}

	if (p_ctx.option("metahuman/textures/configure_import", true) && EditorFileSystem::get_singleton()) {
		bool configured = false;
		Ref<ConfigFile> config;
		config.instantiate();
		if (config->load(path + ".import") == OK && config->has_section_key("remap", "generator_parameters")) {
			const Dictionary generator = config->get_value("remap", "generator_parameters");
			configured = generator.has(TEXTURE_GENERATOR_KEY);
		}
		if (!configured) {
			HashMap<StringName, Variant> settings;
			settings[SNAME("compress/mode")] = 2; // VRAM Compressed.
			settings[SNAME("mipmaps/generate")] = true;
			settings[SNAME("process/size_limit")] = p_ctx.option("metahuman/textures/max_size", 2048);
			if (p_kind == TEXTURE_NORMAL) {
				settings[SNAME("compress/normal_map")] = 1; // Enable.
				// Unreal's normal maps are in DirectX convention.
				settings[SNAME("process/normal_map_invert_y")] = true;
			}
			Dictionary generator;
			generator[TEXTURE_GENERATOR_KEY] = 1;
			EditorFileSystem::get_singleton()->update_file(path);
			EditorFileSystem::get_singleton()->reimport_append(path, settings, String(), generator);
		}
	}

	Ref<Texture2D> texture = ResourceLoader::load(path, "Texture2D");
	if (texture.is_null()) {
		WARN_PRINT(vformat("MetaHuman: texture '%s' could not be loaded.", path));
	}
	p_ctx.textures.insert(path, texture);
	return texture;
}

Ref<Shader> _shader(Context &p_ctx, const String &p_kind) {
	if (const Ref<Shader> *existing = p_ctx.shaders.getptr(p_kind)) {
		return *existing;
	}
	String code;
	if (p_kind == "skin") {
		code = MetaHumanShaders::SKIN;
		if (!p_ctx.option("metahuman/materials/subsurface_scattering", true)) {
			code = code.replace(MetaHumanShaders::SKIN_SSS_LINE, "");
		}
	} else if (p_kind == "eye") {
		code = MetaHumanShaders::EYE;
	} else if (p_kind == "wet") {
		code = MetaHumanShaders::WET;
	} else if (p_kind == "teeth") {
		code = MetaHumanShaders::TEETH;
	} else if (p_kind == "eyelashes") {
		code = MetaHumanShaders::EYELASHES;
	} else if (p_kind == "cloth") {
		code = MetaHumanShaders::CLOTH;
	} else if (p_kind == "hair_strands") {
		code = MetaHumanShaders::HAIR_STRANDS;
	} else if (p_kind == "hair_shell") {
		code = MetaHumanShaders::HAIR_SHELL;
	} else if (p_kind == "hair_strands_opaque") {
		// Dense scalp hair: depth-written and shadowed, so strands sort against each other.
		code = String(MetaHumanShaders::HAIR_STRANDS)
					   .replace("render_mode blend_mix, depth_draw_never, cull_disabled, diffuse_lambert_wrap, specular_schlick_ggx, shadows_disabled;",
							   "render_mode blend_mix, depth_draw_opaque, cull_disabled, diffuse_lambert_wrap, specular_schlick_ggx;")
					   .replace(MetaHumanShaders::HAIR_STRANDS_ALPHA_LINE, "");
	} else if (p_kind == "hair_strands_hashed") {
		// As above, but hashed alpha drops sub-pixel strands by their coverage, so fine flyaways thin out
		// instead of drawing as solid 2 px wires. Grainy unless the project uses TAA.
		code = String(MetaHumanShaders::HAIR_STRANDS)
					   .replace("render_mode blend_mix, depth_draw_never, cull_disabled, diffuse_lambert_wrap, specular_schlick_ggx, shadows_disabled;",
							   "render_mode blend_mix, depth_draw_opaque, cull_disabled, diffuse_lambert_wrap, specular_schlick_ggx;")
					   .replace(MetaHumanShaders::HAIR_STRANDS_ALPHA_LINE,
							   "\tALPHA = pow(clamp(v_coverage, 0.0, 1.0), coverage_exponent);\n\tALPHA_HASH_SCALE = 1.0;\n");
	}
	// The variants above edit HAIR_STRANDS by text; a replace that stops matching would ship the
	// blended shader under another name without a word.
	if (p_kind.begins_with("hair_strands_") && !String(MetaHumanShaders::HAIR_STRANDS).contains(MetaHumanShaders::HAIR_STRANDS_ALPHA_LINE)) {
		ERR_PRINT("MetaHuman: HAIR_STRANDS_ALPHA_LINE no longer matches HAIR_STRANDS, so the " + p_kind + " shader is the blended one.");
	}
	Ref<Shader> shader;
	if (!code.is_empty()) {
		shader.instantiate();
		shader->set_code(code);
		shader->set_name("MetaHuman_" + p_kind);
	}
	p_ctx.shaders.insert(p_kind, shader);
	return shader;
}

Color _color(const Variant &p_value, const Color &p_default) {
	const Array a = p_value;
	if (a.size() < 3) {
		return p_default;
	}
	return Color(a[0], a[1], a[2]);
}

// The texture the manifest gives a material for a role, by its color space.
void _bind_texture(Context &p_ctx, const Ref<ShaderMaterial> &p_material, const Dictionary &p_def, const String &p_role, const StringName &p_uniform, TextureKind p_kind) {
	const Array textures = p_def.get("textures", Array());
	for (int i = 0; i < textures.size(); i++) {
		const Dictionary texture = textures[i];
		if (String(texture.get("role", "")) == p_role) {
			const Ref<Texture2D> loaded = _texture(p_ctx, texture.get("path", ""), p_kind);
			if (loaded.is_valid()) {
				p_material->set_shader_parameter(p_uniform, loaded);
			}
			return;
		}
	}
}

// True for the slots Unreal hides (`M_Hide`): saliva and the ear cartilage.
bool _is_hidden_material(const Dictionary &p_def) {
	return String(p_def.get("display_name", "")) == "M_Hide";
}

Ref<Material> _material(Context &p_ctx, const String &p_id, const String &p_fallback_name) {
	if (const Ref<Material> *existing = p_ctx.materials.getptr(p_id)) {
		return *existing;
	}
	const Dictionary def = p_ctx.material_defs.has(p_id) ? p_ctx.material_defs[p_id] : Dictionary();
	const String type = def.get("type", "");
	const String name = def.get("display_name", p_fallback_name);

	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_name(name);
	if (type == "head" || type == "body") {
		material->set_shader(_shader(p_ctx, "skin"));
		_bind_texture(p_ctx, material, def, "base_color", "base_color_map", TEXTURE_COLOR);
		_bind_texture(p_ctx, material, def, "normal", "normal_map", TEXTURE_NORMAL);
		_bind_texture(p_ctx, material, def, "srmf", "srmf_map", TEXTURE_DATA);
		_bind_texture(p_ctx, material, def, "scatter", "scatter_map", TEXTURE_DATA);
		_bind_texture(p_ctx, material, def, "detail_normal", "detail_normal_map", TEXTURE_NORMAL);
	} else if (type == "eye_ball") {
		material->set_shader(_shader(p_ctx, "eye"));
		_bind_texture(p_ctx, material, def, "sclera_base_color", "sclera_color_map", TEXTURE_COLOR);
		_bind_texture(p_ctx, material, def, "iris_base_color", "iris_color_map", TEXTURE_COLOR);
		_bind_texture(p_ctx, material, def, "sclera_normal", "sclera_normal_map", TEXTURE_NORMAL);
		_bind_texture(p_ctx, material, def, "iris_normal", "iris_normal_map", TEXTURE_NORMAL);
	} else if (type == "eye_shell" || type == "eye_edge") {
		material->set_shader(_shader(p_ctx, "wet"));
	} else if (type == "teeth") {
		material->set_shader(_shader(p_ctx, "teeth"));
		_bind_texture(p_ctx, material, def, "base_color", "base_color_map", TEXTURE_COLOR);
		_bind_texture(p_ctx, material, def, "normal", "normal_map", TEXTURE_NORMAL);
		_bind_texture(p_ctx, material, def, "teeth_mask_002", "mouth_depth_map", TEXTURE_DATA);
	} else if (type == "eyelashes") {
		material->set_shader(_shader(p_ctx, "eyelashes"));
	} else if (type == "clothes") {
		material->set_shader(_shader(p_ctx, "cloth"));
		_bind_texture(p_ctx, material, def, "ambient_occlusion", "ambient_occlusion_map", TEXTURE_DATA);
		_bind_texture(p_ctx, material, def, "stitch_mask", "stitch_mask_map", TEXTURE_DATA);
		_bind_texture(p_ctx, material, def, "normal", "normal_map", TEXTURE_NORMAL);
		_bind_texture(p_ctx, material, def, "micro_normal", "micro_normal_map", TEXTURE_NORMAL);
		const Dictionary fabric = def.get("fabric", Dictionary());
		material->set_shader_parameter("fabric_color", _color(fabric.get("color", Variant()), Color(0.59, 0.59, 0.59)));
		material->set_shader_parameter("stitch_color", _color(fabric.get("stitch_color", Variant()), Color(0.59, 0.59, 0.59)));
		material->set_shader_parameter("micro_scale", fabric.get("micro_scale", 80.0));
		material->set_shader_parameter("normal_strength", fabric.get("normal_strength", 1.0));
		material->set_shader_parameter("micro_normal_strength", fabric.get("micro_normal_strength", 1.0));
	}

	Ref<Material> result = material;
	if (material->get_shader().is_null()) {
		// No manifest, or a slot this importer has no shader for yet.
		Ref<StandardMaterial3D> standard;
		standard.instantiate();
		standard->set_name(name);
		result = standard;
	}
	p_ctx.materials.insert(p_id, result);
	return result;
}

// Meshes -----------------------------------------------------------------------------------------

// The manifest's view of one DNA mesh: its material and the triangles left visible by clothing.
struct MeshInfo {
	String material_id;
	String material_type;
	bool hidden = false;
	Array visible_triangles; // [face, p0, p1, p2] in position indices.
};

void _generate_tangents(Array &r_arrays) {
	// SurfaceTool resets the skin weight count when created from arrays, so it only sees the
	// attributes tangents depend on. It keeps vertex order when given indices.
	Array basic;
	basic.resize(Mesh::ARRAY_MAX);
	basic[Mesh::ARRAY_VERTEX] = r_arrays[Mesh::ARRAY_VERTEX];
	basic[Mesh::ARRAY_NORMAL] = r_arrays[Mesh::ARRAY_NORMAL];
	basic[Mesh::ARRAY_TEX_UV] = r_arrays[Mesh::ARRAY_TEX_UV];
	basic[Mesh::ARRAY_INDEX] = r_arrays[Mesh::ARRAY_INDEX];
	Ref<SurfaceTool> st;
	st.instantiate();
	st->create_from_arrays(basic);
	st->generate_tangents();
	const Array result = st->commit_to_arrays();
	const PackedFloat32Array tangents = result[Mesh::ARRAY_TANGENT];
	const PackedVector3Array vertices = r_arrays[Mesh::ARRAY_VERTEX];
	if (tangents.size() == vertices.size() * 4) {
		r_arrays[Mesh::ARRAY_TANGENT] = tangents;
	} else {
		WARN_PRINT("MetaHuman: tangent generation changed the vertex count; normal maps will be wrong on this mesh.");
	}
}

struct Influence {
	int bone;
	float weight;
};

struct InfluenceStronger {
	bool operator()(const Influence &p_a, const Influence &p_b) const { return p_a.weight > p_b.weight; }
};

// Builds one DNA mesh as an 8-weight skinned surface. Returns false when it has no triangles left.
bool _build_dna_surface(Context &p_ctx, const LoadedDNA &p_dna, uint16_t p_mesh, const MeshInfo &p_info, Array &r_arrays) {
	const dna::BinaryStreamReader *r = p_dna.reader.get();

	const rl4::ConstArrayView<float> px = r->getVertexPositionXs(p_mesh);
	const rl4::ConstArrayView<float> py = r->getVertexPositionYs(p_mesh);
	const rl4::ConstArrayView<float> pz = r->getVertexPositionZs(p_mesh);
	const rl4::ConstArrayView<float> nx = r->getVertexNormalXs(p_mesh);
	const rl4::ConstArrayView<float> ny = r->getVertexNormalYs(p_mesh);
	const rl4::ConstArrayView<float> nz = r->getVertexNormalZs(p_mesh);
	const rl4::ConstArrayView<float> tu = r->getVertexTextureCoordinateUs(p_mesh);
	const rl4::ConstArrayView<float> tv = r->getVertexTextureCoordinateVs(p_mesh);
	const rl4::ConstArrayView<uint32_t> layout_positions = r->getVertexLayoutPositionIndices(p_mesh);
	const rl4::ConstArrayView<uint32_t> layout_uvs = r->getVertexLayoutTextureCoordinateIndices(p_mesh);
	const rl4::ConstArrayView<uint32_t> layout_normals = r->getVertexLayoutNormalIndices(p_mesh);
	const uint32_t face_count = r->getFaceCount(p_mesh);

	// Triangles in DNA layout indices, wound clockwise (Godot's front face; DNA faces are CCW).
	LocalVector<uint32_t> triangles;
	const bool use_visibility = !p_info.visible_triangles.is_empty() && p_ctx.option("metahuman/hide_skin_under_clothing", true);
	if (use_visibility) {
		triangles.reserve(p_info.visible_triangles.size() * 3);
		for (int i = 0; i < p_info.visible_triangles.size(); i++) {
			const Array entry = p_info.visible_triangles[i];
			if (entry.size() < 4) {
				continue;
			}
			const uint32_t face = (int64_t)entry[0];
			ERR_CONTINUE(face >= face_count);
			const rl4::ConstArrayView<uint32_t> face_layouts = r->getFaceVertexLayoutIndices(p_mesh, face);
			uint32_t tri[3];
			bool found = true;
			for (int k = 0; k < 3 && found; k++) {
				const uint32_t position = (int64_t)entry[k + 1];
				found = false;
				for (uint32_t layout : face_layouts) {
					if (layout_positions[layout] == position) {
						tri[k] = layout;
						found = true;
						break;
					}
				}
			}
			if (found) {
				triangles.push_back(tri[0]);
				triangles.push_back(tri[2]);
				triangles.push_back(tri[1]);
			}
		}
	} else {
		for (uint32_t f = 0; f < face_count; f++) {
			const rl4::ConstArrayView<uint32_t> face_layouts = r->getFaceVertexLayoutIndices(p_mesh, f);
			for (uint32_t k = 1; k + 1 < face_layouts.size(); k++) {
				triangles.push_back(face_layouts[0]);
				triangles.push_back(face_layouts[k + 1]);
				triangles.push_back(face_layouts[k]);
			}
		}
	}
	if (triangles.is_empty()) {
		return false;
	}

	// Only the layouts the triangles use become vertices: hidden skin is then not skinned either.
	HashMap<uint32_t, int> vertex_of_layout;
	LocalVector<uint32_t> layouts;
	PackedInt32Array indices;
	indices.resize(triangles.size());
	for (uint32_t i = 0; i < triangles.size(); i++) {
		HashMap<uint32_t, int>::Iterator E = vertex_of_layout.find(triangles[i]);
		if (!E) {
			E = vertex_of_layout.insert(triangles[i], layouts.size());
			layouts.push_back(triangles[i]);
		}
		indices.write[i] = E->value;
	}

	const int vertex_count = layouts.size();
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedInt32Array bones;
	PackedFloat32Array weights;
	vertices.resize(vertex_count);
	normals.resize(vertex_count);
	uvs.resize(vertex_count);
	bones.resize(vertex_count * 8);
	weights.resize(vertex_count * 8);

	LocalVector<Influence> influences;
	for (int v = 0; v < vertex_count; v++) {
		const uint32_t layout = layouts[v];
		const uint32_t p = layout_positions[layout];
		const uint32_t n = layout_normals[layout];
		const uint32_t t = layout_uvs[layout];
		vertices.write[v] = Vector3(px[p], py[p], pz[p]) * MetaHumanDNA::DNA_TO_GODOT_SCALE;
		normals.write[v] = Vector3(nx[n], ny[n], nz[n]).normalized();
		uvs.write[v] = Vector2(tu[t], 1.0 - tv[t]);

		// Godot skins with at most 8 influences; keep the strongest and renormalize.
		const rl4::ConstArrayView<float> w = r->getSkinWeightsValues(p_mesh, p);
		const rl4::ConstArrayView<uint16_t> j = r->getSkinWeightsJointIndices(p_mesh, p);
		influences.clear();
		for (uint32_t k = 0; k < w.size() && k < j.size(); k++) {
			if (w[k] > 0.0f && j[k] < p_dna.joint_bones.size()) {
				influences.push_back({ p_dna.joint_bones[j[k]], w[k] });
			}
		}
		influences.sort_custom<InfluenceStronger>();
		float total = 0.0;
		const uint32_t kept = MIN(influences.size(), 8u);
		for (uint32_t k = 0; k < kept; k++) {
			total += influences[k].weight;
		}
		for (int k = 0; k < 8; k++) {
			const bool used = (uint32_t)k < kept && total > 0.0f;
			bones.write[v * 8 + k] = used ? influences[k].bone : 0;
			weights.write[v * 8 + k] = used ? influences[k].weight / total : 0.0f;
		}
	}

	r_arrays.resize(Mesh::ARRAY_MAX);
	r_arrays[Mesh::ARRAY_VERTEX] = vertices;
	r_arrays[Mesh::ARRAY_NORMAL] = normals;
	r_arrays[Mesh::ARRAY_TEX_UV] = uvs;
	r_arrays[Mesh::ARRAY_BONES] = bones;
	r_arrays[Mesh::ARRAY_WEIGHTS] = weights;
	r_arrays[Mesh::ARRAY_INDEX] = indices;
	if (p_ctx.flags & EditorSceneFormatImporter::IMPORT_GENERATE_TANGENT_ARRAYS) {
		_generate_tangents(r_arrays);
	}
	return true;
}

HashMap<int, MeshInfo> _mesh_infos(Context &p_ctx, const String &p_role) {
	HashMap<int, MeshInfo> infos;
	const Array geometries = p_ctx.manifest.get("dna_geometry", Array());
	for (int g = 0; g < geometries.size(); g++) {
		const Dictionary geometry = geometries[g];
		if (String(geometry.get("role", "")) != p_role) {
			continue;
		}
		const Array meshes = geometry.get("meshes", Array());
		for (int i = 0; i < meshes.size(); i++) {
			const Dictionary mesh = meshes[i];
			const Dictionary material = mesh.get("material", Dictionary());
			MeshInfo info;
			info.material_id = material.get("name", "");
			info.material_type = material.get("type", "");
			const Dictionary def = p_ctx.material_defs.has(info.material_id) ? p_ctx.material_defs[info.material_id] : Dictionary();
			info.hidden = _is_hidden_material(def);
			infos.insert((int)mesh.get("mesh_index", -1), info);
		}
		// Visibility under clothing, from the geometry file.
		const String geometry_path = geometry.get("path", "");
		if (geometry_path.is_empty() || !p_ctx.option("metahuman/hide_skin_under_clothing", true)) {
			continue;
		}
		const Dictionary geometry_data = _load_json(p_ctx.folder.path_join(geometry_path));
		const Array geometry_meshes = geometry_data.get("meshes", Array());
		for (int i = 0; i < geometry_meshes.size(); i++) {
			const Dictionary mesh = geometry_meshes[i];
			MeshInfo *info = infos.getptr((int)mesh.get("mesh_index", -1));
			if (info && bool(mesh.get("visibility_valid", false))) {
				info->visible_triangles = mesh.get("visible_triangles", Array());
			}
		}
	}
	return infos;
}

// Scales the skin's tiling pore normal to the mesh. The map repeats in UV space, and the body's UVs
// cover ~3.4x more skin per unit than the face's (Ava: median 1.34 against 0.40 m per UV unit), so a
// fixed tiling gave the body pores 3.4x the face's size. One repeat covers
// `metahuman/materials/skin_detail_size` of skin (1.2 cm: the map's ~1,300 pore marks per repeat then
// land at ~900 per cm², the face's follicle density, and its detail sits below the ~1 mm the baked 8K
// normal resolves; webgpu_notes/work/results/25-metahuman-skin-coarse). The median per-triangle
// density is used, so the densely triangulated face, not the scalp or neck, sets the size.
// The detail also fades out at the end of LOD 0, as Unreal turns it off from face LOD 1.
void _fit_skin_detail(Context &p_ctx, const Ref<Material> &p_material, const Array &p_arrays, float p_lod0_end) {
	const Ref<ShaderMaterial> material = p_material;
	if (material.is_null()) {
		return;
	}
	const PackedVector3Array vertices = p_arrays[Mesh::ARRAY_VERTEX];
	const PackedVector2Array uvs = p_arrays[Mesh::ARRAY_TEX_UV];
	const PackedInt32Array indices = p_arrays[Mesh::ARRAY_INDEX];
	if (uvs.size() != vertices.size()) {
		return;
	}
	LocalVector<float> densities;
	densities.reserve(indices.size() / 3);
	for (int i = 0; i + 2 < indices.size(); i += 3) {
		const int a = indices[i], b = indices[i + 1], c = indices[i + 2];
		const float area = (vertices[b] - vertices[a]).cross(vertices[c] - vertices[a]).length();
		const float uv_area = Math::abs((uvs[b] - uvs[a]).cross(uvs[c] - uvs[a]));
		if (area > 0.0f && uv_area > 1e-12f) {
			densities.push_back(Math::sqrt(area / uv_area));
		}
	}
	if (densities.is_empty()) {
		return;
	}
	densities.sort();
	const float meters_per_uv = densities[densities.size() / 2];
	const float detail_size = MAX(0.001f, p_ctx.option("metahuman/materials/skin_detail_size", 0.012f));
	material->set_shader_parameter("detail_normal_tiling", meters_per_uv / detail_size);
	if (p_lod0_end > 0.0f) {
		material->set_shader_parameter("detail_fade_begin", p_lod0_end * 0.5f);
		material->set_shader_parameter("detail_fade_end", p_lod0_end);
	} else {
		// A single LOD: no switch to fade at.
		material->set_shader_parameter("detail_fade_begin", 1e5f);
		material->set_shader_parameter("detail_fade_end", 2e5f);
	}
}

void _add_dna_meshes(Context &p_ctx, const LoadedDNA &p_dna, const String &p_role, const float *p_default_distances, int p_default_distance_count, int p_max_lods) {
	const dna::BinaryStreamReader *r = p_dna.reader.get();
	const HashMap<int, MeshInfo> infos = _mesh_infos(p_ctx, p_role);
	const float distance_scale = p_ctx.option("metahuman/lods/distance_scale", 1.0f);
	const int lod_count = MIN((int)r->getLODCount(), p_max_lods);

	for (int lod = 0; lod < lod_count; lod++) {
		const float begin = lod > 0 ? p_default_distances[MIN(lod - 1, p_default_distance_count - 1)] * distance_scale : 0.0;
		const float end = lod + 1 < lod_count ? p_default_distances[MIN(lod, p_default_distance_count - 1)] * distance_scale : 0.0;
		for (uint16_t mesh_index : r->getMeshIndicesForLOD(lod)) {
			const String dna_name = _dna_string(r->getMeshName(mesh_index));
			MeshInfo info;
			if (const MeshInfo *found = infos.getptr(mesh_index)) {
				info = *found;
			}
			// The material comes from the mesh's name (`eyeLeft_lod3_mesh` -> `eyeLeft_shader_shader`).
			// The manifest's per-mesh material list cannot be trusted past LOD 2: it repeats LOD 0's
			// nine slots for every LOD, but saliva and cartilage are gone from LOD 3 on, so from there
			// it gives each mesh its neighbor's material (webgpu_notes/TASKS.md §2.33).
			const int lod_suffix = dna_name.find("_lod");
			const String slot = (lod_suffix > 0 ? dna_name.substr(0, lod_suffix) : dna_name) + "_shader_shader";
			if (const String *id = p_ctx.material_by_slot.getptr(slot)) {
				const Dictionary &def = p_ctx.material_defs[*id];
				info.material_id = *id;
				info.material_type = def.get("type", "");
				info.hidden = _is_hidden_material(def);
			}
			if (info.hidden || (info.material_type == "eyelashes" && p_ctx.groom_eyelashes)) {
				continue;
			}
			Array arrays;
			if (!_build_dna_surface(p_ctx, p_dna, mesh_index, info, arrays)) {
				continue;
			}
			if (dna_name == "head_lod0_mesh") {
				p_ctx.head_surface.set_from_arrays(arrays);
				const PackedVector3Array head_vertices = arrays[Mesh::ARRAY_VERTEX];
				for (const Vector3 &v : head_vertices) {
					p_ctx.head_vertices.push_back(v);
				}
			}
			const String material_id = info.material_id.is_empty() ? dna_name : info.material_id;
			const Ref<Material> material = _material(p_ctx, material_id, dna_name.trim_suffix("_mesh"));
			if (lod == 0 && (info.material_type == "head" || info.material_type == "body")) {
				_fit_skin_detail(p_ctx, material, arrays, end);
			}

			Ref<ImporterMesh> mesh;
			mesh.instantiate();
			mesh->set_name(dna_name.trim_suffix("_mesh"));
			mesh->add_surface(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), material, material.is_valid() ? material->get_name() : String(), Mesh::ARRAY_FLAG_USE_8_BONE_WEIGHTS);

			ImporterMeshInstance3D *instance = memnew(ImporterMeshInstance3D);
			instance->set_name(dna_name.trim_suffix("_mesh"));
			instance->set_mesh(mesh);
			instance->set_skin(p_ctx.skin);
			instance->set_skeleton_path(NodePath(".."));
			instance->set_visibility_range_begin(begin);
			instance->set_visibility_range_end(end);
			if (info.material_type == "eye_shell" || info.material_type == "eye_edge" || info.material_type == "eyelashes" || info.material_type == "teeth") {
				// Small, inside or transparent: their shadows cost a draw each for nothing visible.
				instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			}
			p_ctx.skeleton->add_child(instance, true);
			instance->set_owner(p_ctx.skeleton->get_owner());
		}
	}
}

// Grooms -----------------------------------------------------------------------------------------

enum GroomKind {
	GROOM_SCALP,
	GROOM_EYEBROWS,
	GROOM_EYELASHES,
	GROOM_FUZZ,
	GROOM_UNKNOWN,
};

enum ScalpMode {
	SCALP_NONE,
	SCALP_SHELL,
	// Decimated strands up close, the shell beyond metahuman/hair/scalp_strand_distance.
	SCALP_STRANDS,
};

enum ScalpCoverage {
	SCALP_COVERAGE_SOLID,
	SCALP_COVERAGE_HASHED,
};

enum StrandMode {
	STRANDS_NONE,
	STRANDS_ON,
};

enum EyelashMode {
	EYELASHES_DNA_CARDS,
	EYELASHES_STRANDS,
};

struct GroomSource {
	String path;
	GroomKind kind = GROOM_UNKNOWN;
	Dictionary material; // The manifest's material, with hair_color.
	Dictionary settings; // The component's groom settings (root_scale, tip_scale, ...).
};

GroomKind _groom_kind(const String &p_name, const String &p_material_type) {
	const String type = p_material_type.to_lower();
	const String name = p_name.to_lower();
	if (type == "eyebrows" || name.contains("eyebrow")) {
		return GROOM_EYEBROWS;
	}
	if (type == "eyelashes_groom" || name.contains("eyelash")) {
		return GROOM_EYELASHES;
	}
	if (type == "peach_fuzz" || name.contains("fuzz")) {
		return GROOM_FUZZ;
	}
	if (type == "hair" || name == "hair" || name.contains("hair")) {
		return GROOM_SCALP;
	}
	return GROOM_UNKNOWN;
}

// The grooms of the export: from the manifest's alembic components, else Grooms/*.abc by file name.
Vector<GroomSource> _groom_sources(const Context &p_ctx) {
	Vector<GroomSource> sources;
	const Array components = p_ctx.manifest.get("components", Array());
	for (int i = 0; i < components.size(); i++) {
		const Dictionary component = components[i];
		if (String(component.get("type", "")) != "alembic") {
			continue;
		}
		GroomSource source;
		source.path = p_ctx.folder.path_join(String(component.get("path", "")));
		source.settings = component.get("groom", Dictionary());
		const Array materials = component.get("materials", Array());
		if (!materials.is_empty()) {
			const String id = Dictionary(materials[0]).get("name", "");
			if (p_ctx.material_defs.has(id)) {
				source.material = p_ctx.material_defs[id];
			}
		}
		source.kind = _groom_kind(component.get("name", ""), source.material.get("type", ""));
		sources.push_back(source);
	}
	if (sources.is_empty()) {
		const String dir = p_ctx.folder.path_join("Grooms");
		for (const char *name : { "hair", "eyebrows", "eyelashes", "fuzz" }) {
			const String path = dir.path_join(String(name) + ".abc");
			if (FileAccess::exists(path)) {
				GroomSource source;
				source.path = path;
				source.kind = _groom_kind(name, "");
				sources.push_back(source);
			}
		}
	}
	return sources;
}

// First and last colors of a manifest color ramp.
void _ramp_ends(const Dictionary &p_hair_color, const String &p_ramp, const Color &p_default, Color &r_first, Color &r_last) {
	const Dictionary ramps = p_hair_color.get("ramps", Dictionary());
	const Array ramp = ramps.get(p_ramp, Array());
	r_first = p_default;
	r_last = p_default;
	if (ramp.is_empty()) {
		return;
	}
	r_first = _color(Dictionary(ramp[0]).get("color", Variant()), p_default);
	r_last = _color(Dictionary(ramp[ramp.size() - 1]).get("color", Variant()), p_default);
}

Ref<Material> _hair_material(Context &p_ctx, const GroomSource &p_source, bool p_shell, float p_max_width, bool p_has_groom_color) {
	const Dictionary hair_color = p_source.material.get("hair_color", Dictionary());
	const Color base = _color(hair_color.get("color", Variant()), Color(0.02, 0.015, 0.01));
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_name(String(p_source.material.get("display_name", p_source.path.get_file().get_basename())) + (p_shell ? "_Shell" : "_Strands"));
	material->set_shader(_shader(p_ctx, p_shell ? "hair_shell" : "hair_strands"));
	// Unreal's hair roughness drives a Marschner model; under Godot's GGX the same number gives a
	// harsh, plastic highlight. Keep it from going below 0.5.
	material->set_shader_parameter("roughness", MAX((float)hair_color.get("roughness", 0.4), p_shell ? 0.7f : 0.5f));
	// The manifest's colors are linear; source_color uniforms take sRGB.
	const bool use_groom_color = p_has_groom_color && hair_color.is_empty();
	material->set_shader_parameter("use_groom_color", use_groom_color ? 1.0 : 0.0);
	if (p_shell) {
		// A shell is lit like an opaque surface, but hair reads lighter than its strands' color because
		// light scatters through it; without that a dark groom renders as black plastic. The square
		// root lifts dark colors most (0.0003 -> 0.017 linear) and leaves light ones nearly alone.
		const Color lifted(Math::sqrt(base.r), Math::sqrt(base.g), Math::sqrt(base.b));
		material->set_shader_parameter("hair_color", lifted.linear_to_srgb());
	} else {
		Color root, tip, variation_a, variation_b;
		_ramp_ends(hair_color, "root_to_tip", base, root, tip);
		_ramp_ends(hair_color, "variation", base, variation_a, variation_b);
		// The variation ramp holds absolute colors; the shader scales the root-to-tip color by it, so
		// pass it as per-channel factors around the base color (linear, no sRGB hint).
		const auto factor = [&base](const Color &p_color) {
			return Vector3(p_color.r / MAX(base.r, 1e-5f), p_color.g / MAX(base.g, 1e-5f), p_color.b / MAX(base.b, 1e-5f)).clampf(0.0, 4.0);
		};
		material->set_shader_parameter("root_color", root.linear_to_srgb());
		material->set_shader_parameter("tip_color", tip.linear_to_srgb());
		material->set_shader_parameter("variation_a", factor(variation_a));
		material->set_shader_parameter("variation_b", factor(variation_b));
		material->set_shader_parameter("max_width", p_max_width);
		material->set_shader_parameter("root_scale", p_source.settings.get("root_scale", 1.0));
		material->set_shader_parameter("tip_scale", p_source.settings.get("tip_scale", 0.5));
	}
	return material;
}

// Root-bound groom vertices rarely use more than a few joints; 4 weights halve their skinning cost.
void _reduce_to_four_weights(Array &r_arrays) {
	const PackedInt32Array bones = r_arrays[Mesh::ARRAY_BONES];
	const PackedFloat32Array weights = r_arrays[Mesh::ARRAY_WEIGHTS];
	const int count = bones.size() / 8;
	PackedInt32Array out_bones;
	PackedFloat32Array out_weights;
	out_bones.resize(count * 4);
	out_weights.resize(count * 4);
	for (int v = 0; v < count; v++) {
		// The 8 weights are sorted strongest first by build_strands()/build_shell()'s source.
		int order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
		for (int i = 1; i < 8; i++) {
			for (int j = i; j > 0 && weights[v * 8 + order[j]] > weights[v * 8 + order[j - 1]]; j--) {
				SWAP(order[j], order[j - 1]);
			}
		}
		float total = 0.0;
		for (int k = 0; k < 4; k++) {
			total += weights[v * 8 + order[k]];
		}
		for (int k = 0; k < 4; k++) {
			out_bones.set(v * 4 + k, bones[v * 8 + order[k]]);
			out_weights.set(v * 4 + k, total > 0.0f ? weights[v * 8 + order[k]] / total : (k == 0 ? 1.0f : 0.0f));
		}
	}
	r_arrays[Mesh::ARRAY_BONES] = out_bones;
	r_arrays[Mesh::ARRAY_WEIGHTS] = out_weights;
}

// One way a groom is drawn: strands or a shell, visible over a range of camera distances.
struct GroomRepresentation {
	bool shell = false;
	float visible_from = 0.0;
	float visible_to = 0.0; // 0 is always.
	bool opaque = false; // Strands only: opaque, depth-written and shadowed, for dense scalp hair.
	float strand_fraction = 1.0;
	int max_points = 0;
	float crossfade = 0.0; // Meters of dithered fade at either end of the range.
	bool shadows_only = false; // Shell only: casts the hair's shadow for strands, which cast none.
};

// A groom's simulation, built once and shared by its representations.
struct GroomDynamics {
	Ref<MetaHumanHairSimulation> simulation;
	LocalVector<Vector3> guide_points; // Rest, skeleton space, guide_count * point_count.
	int point_count = 0;
	PackedStringArray mesh_names;
};

// Collider spheres for the scalp groom: the skull, fitted to the head's vertices above the head joint,
// then the neck, the upper spine and the shoulders.
void _add_colliders(const Context &p_ctx, const Ref<MetaHumanHairSimulation> &p_simulation, const LocalVector<Vector3> &p_head_vertices) {
	PackedInt32Array bones;
	PackedVector3Array offsets;
	PackedFloat32Array radii;
	const auto add = [&](const String &p_bone, const Vector3 &p_center, float p_radius) {
		const int bone = p_ctx.skeleton->find_bone(p_bone);
		if (bone < 0) {
			return;
		}
		bones.push_back(bone);
		offsets.push_back(p_ctx.global_rests[bone].affine_inverse().xform(p_center));
		radii.push_back(p_radius);
	};
	const int head = p_ctx.skeleton->find_bone("head");
	if (head >= 0 && !p_head_vertices.is_empty()) {
		const float head_y = p_ctx.global_rests[head].origin.y;
		Vector3 sum;
		int count = 0;
		for (const Vector3 &v : p_head_vertices) {
			if (v.y > head_y + 0.03) {
				sum += v;
				count++;
			}
		}
		if (count > 0) {
			const Vector3 center = sum / count;
			float distance = 0.0;
			for (const Vector3 &v : p_head_vertices) {
				if (v.y > head_y + 0.03) {
					distance += v.distance_to(center);
				}
			}
			// The mean distance overstates the radius where the skull is narrow; hair sits on it.
			add("head", center, distance / count * 0.85);
		}
	}
	for (const char *neck : { "neck_01", "neck_02" }) {
		const int bone = p_ctx.skeleton->find_bone(neck);
		if (bone >= 0) {
			add(neck, p_ctx.global_rests[bone].origin, 0.055);
		}
	}
	const int spine = p_ctx.skeleton->find_bone("spine_05");
	if (spine >= 0) {
		add("spine_05", p_ctx.global_rests[spine].origin, 0.1);
	}
	for (const char *side : { "l", "r" }) {
		const int clavicle = p_ctx.skeleton->find_bone(String("clavicle_") + side);
		const int upperarm = p_ctx.skeleton->find_bone(String("upperarm_") + side);
		if (clavicle >= 0 && upperarm >= 0) {
			add(String("clavicle_") + side, p_ctx.global_rests[clavicle].origin.lerp(p_ctx.global_rests[upperarm].origin, 0.6), 0.065);
		}
	}
	p_simulation->set_collider_bones(bones);
	p_simulation->set_collider_offsets(offsets);
	p_simulation->set_collider_radii(radii);
}

bool _build_dynamics(Context &p_ctx, const MetaHumanGroom &p_groom, GroomDynamics &r_dynamics) {
	const int point_count = p_ctx.option("metahuman/hair/guide_points", 16);
	PackedVector3Array rest;
	PackedInt32Array root_bones;
	PackedFloat32Array root_weights;
	LocalVector<Vector3> points;
	int bones8[8];
	float weights8[8];
	for (uint32_t c = 0; c < p_groom.get_curve_count(); c++) {
		if (!p_groom.is_guide[c]) {
			continue;
		}
		MetaHumanHair::resample_curve(p_groom, c, point_count, points);
		for (const Vector3 &q : points) {
			rest.push_back(q);
			r_dynamics.guide_points.push_back(q);
		}
		// The root binding's 4 strongest weights, renormalized.
		p_ctx.head_surface.get_weights(p_ctx.head_surface.nearest(points[0]), bones8, weights8);
		float total = 0.0;
		for (int k = 0; k < 4; k++) {
			total += weights8[k];
		}
		for (int k = 0; k < 4; k++) {
			root_bones.push_back(bones8[k]);
			root_weights.push_back(total > 0.0f ? weights8[k] / total : (k == 0 ? 1.0f : 0.0f));
		}
	}
	if (rest.is_empty()) {
		return false;
	}
	r_dynamics.point_count = point_count;
	r_dynamics.simulation.instantiate();
	r_dynamics.simulation->set_name("HairSimulation");
	r_dynamics.simulation->set_point_count(point_count);
	r_dynamics.simulation->set_rest_points(rest);
	r_dynamics.simulation->set_root_bones(root_bones);
	r_dynamics.simulation->set_root_weights(root_weights);
	_add_colliders(p_ctx, r_dynamics.simulation, p_ctx.head_vertices);
	return true;
}

void _add_groom_instance(Context &p_ctx, const GroomSource &p_source, const MetaHumanGroom &p_groom, const GroomRepresentation &p_rep, GroomDynamics *p_dynamics) {
	const float distance_scale = p_ctx.option("metahuman/lods/distance_scale", 1.0f);
	Array arrays;
	float max_width = 0.0;
	bool built = false;
	if (p_rep.shell) {
		MetaHumanHair::ShellSettings settings;
		settings.voxel_size = p_ctx.option("metahuman/hair/shell_voxel_size_mm", 3.0f) * 0.001f;
		settings.target_triangles = p_ctx.option("metahuman/hair/shell_triangles", 8000);
		if (p_dynamics) {
			settings.guide_points = &p_dynamics->guide_points;
			settings.guide_point_count = p_dynamics->point_count;
		}
		built = MetaHumanHair::build_shell(p_groom, p_ctx.head_surface, settings, arrays);
	} else {
		MetaHumanHair::StrandSettings settings;
		settings.strand_fraction = p_rep.strand_fraction;
		settings.max_points = p_rep.max_points;
		settings.with_guides = p_dynamics != nullptr;
		built = MetaHumanHair::build_strands(p_groom, p_ctx.head_surface, settings, arrays, max_width);
	}
	if (!built) {
		WARN_PRINT(vformat("MetaHuman: groom '%s' produced no geometry.", p_source.path));
		return;
	}
	_reduce_to_four_weights(arrays);

	Ref<Material> material = _hair_material(p_ctx, p_source, p_rep.shell, max_width, !p_groom.colors.is_empty());
	if (!p_rep.shell && p_rep.opaque) {
		Ref<ShaderMaterial> strands = material;
		const bool hashed = p_ctx.option("metahuman/hair/scalp_strand_coverage", (int)SCALP_COVERAGE_SOLID) == SCALP_COVERAGE_HASHED;
		strands->set_shader(_shader(p_ctx, hashed ? "hair_strands_hashed" : "hair_strands_opaque"));
		strands->set_shader_parameter("root_darkening", 0.5);
	}
	const String name = p_source.path.get_file().get_basename() + (p_rep.shell ? (p_rep.shadows_only ? "_shadow" : "_shell") : "_strands");
	Ref<ImporterMesh> mesh;
	mesh.instantiate();
	mesh->set_name(name);
	const bool has_guides = arrays[Mesh::ARRAY_CUSTOM0].get_type() == Variant::PACKED_FLOAT32_ARRAY;
	const uint64_t format = has_guides ? (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) : 0;
	mesh->add_surface(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), material, material->get_name(), format);
	if (has_guides && p_dynamics) {
		p_dynamics->mesh_names.push_back(name);
	}

	ImporterMeshInstance3D *instance = memnew(ImporterMeshInstance3D);
	instance->set_name(name);
	instance->set_mesh(mesh);
	instance->set_skin(p_ctx.skin);
	instance->set_skeleton_path(NodePath(".."));
	instance->set_visibility_range_begin(p_rep.visible_from * distance_scale);
	instance->set_visibility_range_end(p_rep.visible_to * distance_scale);
	if (p_rep.crossfade > 0.0) {
		// Dither from one representation to the next instead of popping.
		instance->set_visibility_range_begin_margin(p_rep.visible_from > 0.0 ? p_rep.crossfade * distance_scale : 0.0);
		instance->set_visibility_range_end_margin(p_rep.visible_to > 0.0 ? p_rep.crossfade * distance_scale : 0.0);
		instance->set_visibility_range_fade_mode(GeometryInstance3D::VISIBILITY_RANGE_FADE_SELF);
	}
	if (!p_rep.shell) {
		// Blended strands draw no shadow anyway. Opaque scalp strands would, but hundreds of thousands
		// of triangles in every shadow map cost more than the hair's shadow is worth: a shadows-only
		// shell casts it instead (the shell's own shading differs, its shadow hardly).
		instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	} else if (p_rep.shadows_only) {
		instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_SHADOWS_ONLY);
	}
	p_ctx.skeleton->add_child(instance, true);
	instance->set_owner(p_ctx.skeleton->get_owner());
	const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
	print_verbose(vformat("MetaHuman: groom '%s': %d curves, %d triangles as %s.", p_source.path, p_groom.get_curve_count(), indices.size() / 3, p_rep.shell ? "a shell" : "strands"));
}

void _add_grooms(Context &p_ctx, const Vector<GroomSource> &p_sources) {
	ERR_FAIL_COND_MSG(p_ctx.head_surface.is_empty(), "MetaHuman: no head LOD 0 to bind the grooms to.");
	const float strand_fraction = p_ctx.option("metahuman/hair/strand_fraction", 1.0f);
	for (const GroomSource &source : p_sources) {
		Vector<GroomRepresentation> reps;
		switch (source.kind) {
			case GROOM_SCALP: {
				const int mode = p_ctx.option("metahuman/hair/scalp", (int)SCALP_STRANDS);
				const float switch_distance = p_ctx.option("metahuman/hair/scalp_strand_distance", 3.0f);
				if (mode == SCALP_STRANDS) {
					GroomRepresentation strands;
					strands.opaque = true;
					strands.strand_fraction = p_ctx.option("metahuman/hair/scalp_strand_fraction", 0.2f);
					strands.max_points = p_ctx.option("metahuman/hair/scalp_strand_points", 10);
					strands.visible_to = switch_distance;
					strands.crossfade = 0.5;
					reps.push_back(strands);
				}
				if (mode == SCALP_STRANDS) {
					GroomRepresentation shadow;
					shadow.shell = true;
					shadow.shadows_only = true;
					shadow.visible_to = switch_distance;
					reps.push_back(shadow);
				}
				if (mode == SCALP_STRANDS || mode == SCALP_SHELL) {
					GroomRepresentation shell;
					shell.shell = true;
					shell.visible_from = mode == SCALP_STRANDS ? switch_distance : 0.0f;
					shell.crossfade = mode == SCALP_STRANDS ? 0.5f : 0.0f;
					reps.push_back(shell);
				}
			} break;
			case GROOM_EYEBROWS:
				if (p_ctx.option("metahuman/hair/eyebrows", (int)STRANDS_ON) == STRANDS_ON) {
					GroomRepresentation strands;
					strands.strand_fraction = strand_fraction;
					strands.visible_to = FACE_LOD_DISTANCES[3];
					reps.push_back(strands);
				}
				break;
			case GROOM_EYELASHES:
				if (p_ctx.groom_eyelashes) {
					GroomRepresentation strands;
					strands.strand_fraction = strand_fraction;
					strands.visible_to = FACE_LOD_DISTANCES[3];
					reps.push_back(strands);
				}
				break;
			case GROOM_FUZZ:
				if (p_ctx.option("metahuman/hair/fuzz", (int)STRANDS_NONE) == STRANDS_ON) {
					GroomRepresentation strands;
					strands.strand_fraction = strand_fraction;
					strands.visible_to = FACE_LOD_DISTANCES[0];
					reps.push_back(strands);
				}
				break;
			case GROOM_UNKNOWN:
				WARN_PRINT(vformat("MetaHuman: skipping groom '%s': not scalp hair, eyebrows, eyelashes or fuzz.", source.path));
				break;
		}
		if (reps.is_empty()) {
			continue;
		}
		MetaHumanGroom groom;
		if (MetaHumanGroom::load(source.path, groom) != OK) {
			continue;
		}
		GroomDynamics dynamics;
		const bool dynamic = source.kind == GROOM_SCALP && p_ctx.option("metahuman/hair/dynamic", true) && _build_dynamics(p_ctx, groom, dynamics);
		for (const GroomRepresentation &rep : reps) {
			_add_groom_instance(p_ctx, source, groom, rep, dynamic ? &dynamics : nullptr);
		}
		if (dynamic && !dynamics.mesh_names.is_empty()) {
			MetaHumanHairSimulator *simulator = memnew(MetaHumanHairSimulator);
			simulator->set_name(source.path.get_file().get_basename().capitalize().replace(" ", "") + "Simulator");
			simulator->set_simulation(dynamics.simulation);
			TypedArray<NodePath> meshes;
			for (const String &mesh_name : dynamics.mesh_names) {
				meshes.push_back(NodePath("../" + mesh_name));
			}
			simulator->set_hair_meshes(meshes);
			// After MetaHumanRig in the modifier order: the hair follows the posed face and neck.
			p_ctx.pending_modifiers.push_back(simulator);
		}
	}
}

// Clothing ---------------------------------------------------------------------------------------

#ifdef MODULE_FBX_ENABLED
void _collect_importer_meshes(Node *p_node, LocalVector<ImporterMeshInstance3D *> &r_list) {
	if (ImporterMeshInstance3D *mi = Object::cast_to<ImporterMeshInstance3D>(p_node)) {
		r_list.push_back(mi);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_importer_meshes(p_node->get_child(i), r_list);
	}
}

// The transform from p_node's space to p_root's, for nodes not in a tree.
Transform3D _transform_to(Node *p_node, Node *p_root) {
	Transform3D xform;
	for (Node *n = p_node; n && n != p_root; n = n->get_parent()) {
		if (Node3D *n3d = Object::cast_to<Node3D>(n)) {
			xform = n3d->get_transform() * xform;
		}
	}
	return xform;
}

// FBX exports name the root `Root`; the DNA calls it `root`.
int _find_bone_any_case(Skeleton3D *p_skeleton, const String &p_name) {
	const int bone = p_skeleton->find_bone(p_name);
	if (bone >= 0) {
		return bone;
	}
	const String lower = p_name.to_lower();
	for (int i = 0; i < p_skeleton->get_bone_count(); i++) {
		if (p_skeleton->get_bone_name(i).to_lower() == lower) {
			return i;
		}
	}
	return -1;
}

Skeleton3D *_find_skeleton(Node *p_node) {
	if (Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node)) {
		return skeleton;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (Skeleton3D *skeleton = _find_skeleton(p_node->get_child(i))) {
			return skeleton;
		}
	}
	return nullptr;
}

void _add_clothing(Context &p_ctx, const Dictionary &p_component) {
	const String path = p_ctx.folder.path_join(String(p_component.get("path", "")));
	if (!FileAccess::exists(path)) {
		WARN_PRINT(vformat("MetaHuman: clothing '%s' is missing.", path));
		return;
	}
	Ref<FBXDocument> fbx;
	fbx.instantiate();
	Ref<FBXState> state;
	state.instantiate();
	state->set_handle_binary_image_mode(GLTFState::HANDLE_BINARY_IMAGE_MODE_DISCARD_TEXTURES);
	Error err = fbx->append_from_file(ProjectSettings::get_singleton()->globalize_path(path), state, EditorSceneFormatImporter::IMPORT_USE_NAMED_SKIN_BINDS, path.get_base_dir());
	ERR_FAIL_COND_MSG(err != OK, vformat("MetaHuman: could not read the clothing '%s'.", path));
	Node *root = fbx->generate_scene(state);
	ERR_FAIL_NULL(root);

	Skeleton3D *fbx_skeleton = _find_skeleton(root);
	const Transform3D skeleton_to_scene = fbx_skeleton ? _transform_to(fbx_skeleton, root) : Transform3D();
	LocalVector<ImporterMeshInstance3D *> instances;
	_collect_importer_meshes(root, instances);

	// Materials by slot name, from the component's material list.
	HashMap<String, String> material_by_slot;
	HashMap<int, String> material_by_index;
	const Array component_materials = p_component.get("materials", Array());
	for (int i = 0; i < component_materials.size(); i++) {
		const Dictionary m = component_materials[i];
		material_by_slot.insert(m.get("slot", ""), m.get("name", ""));
		material_by_index.insert((int)m.get("source_index", i), m.get("name", ""));
	}

	double worst_rest_mismatch = 0.0;
	for (ImporterMeshInstance3D *source : instances) {
		const Ref<ImporterMesh> source_mesh = source->get_mesh();
		const Ref<Skin> source_skin = source->get_skin();
		if (source_mesh.is_null()) {
			continue;
		}

		// Bind index in the FBX skin -> our bone, and the vertex transform from the FBX's bind
		// space into our skeleton's rest space.
		LocalVector<int> bind_bones;
		LocalVector<Transform3D> bind_to_rest;
		if (source_skin.is_valid()) {
			for (int b = 0; b < source_skin->get_bind_count(); b++) {
				StringName bind_name = source_skin->get_bind_name(b);
				if (bind_name == StringName() && fbx_skeleton && source_skin->get_bind_bone(b) >= 0) {
					bind_name = fbx_skeleton->get_bone_name(source_skin->get_bind_bone(b));
				}
				const int bone = _find_bone_any_case(p_ctx.skeleton, bind_name);
				bind_bones.push_back(bone);
				if (bone < 0) {
					WARN_PRINT(vformat("MetaHuman: clothing bone '%s' is not in the skeleton; its vertices follow the root.", bind_name));
					bind_to_rest.push_back(Transform3D());
					continue;
				}
				// At rest the FBX skins a vertex to G_fbx * bind_fbx * v, in its skeleton's space; its
				// scene then turns that Y-up (FBX skeletons from Unreal are Z-up). Place the vertex
				// there, which is our rest space when both skeletons agree.
				Transform3D fbx_global = skeleton_to_scene;
				if (fbx_skeleton) {
					const int fbx_bone = fbx_skeleton->find_bone(bind_name);
					if (fbx_bone >= 0) {
						fbx_global = skeleton_to_scene * fbx_skeleton->get_bone_global_rest(fbx_bone);
					}
				}
				bind_to_rest.push_back(fbx_global * source_skin->get_bind_pose(b));
				worst_rest_mismatch = MAX(worst_rest_mismatch, (double)fbx_global.origin.distance_to(p_ctx.global_rests[bone].origin));
			}
		}

		Ref<ImporterMesh> mesh;
		mesh.instantiate();
		mesh->set_name(source_mesh->get_name());
		for (int s = 0; s < source_mesh->get_surface_count(); s++) {
			Array arrays = source_mesh->get_surface_arrays(s);
			const uint64_t format = source_mesh->get_surface_format(s);
			const int weights_per_vertex = (format & Mesh::ARRAY_FLAG_USE_8_BONE_WEIGHTS) ? 8 : 4;
			PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
			PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
			PackedFloat32Array tangents = arrays[Mesh::ARRAY_TANGENT];
			PackedInt32Array bones = arrays[Mesh::ARRAY_BONES];
			const PackedFloat32Array weights = arrays[Mesh::ARRAY_WEIGHTS];
			const bool skinned = !bind_bones.is_empty() && bones.size() == vertices.size() * weights_per_vertex;
			for (int v = 0; v < vertices.size(); v++) {
				Transform3D to_rest;
				if (skinned) {
					// The strongest influence's bind space decides; for a consistent FBX all agree.
					int best = 0;
					for (int k = 1; k < weights_per_vertex; k++) {
						if (weights[v * weights_per_vertex + k] > weights[v * weights_per_vertex + best]) {
							best = k;
						}
					}
					const int bind = bones[v * weights_per_vertex + best];
					if (bind >= 0 && bind < (int)bind_to_rest.size()) {
						to_rest = bind_to_rest[bind];
					}
					for (int k = 0; k < weights_per_vertex; k++) {
						const int b = bones[v * weights_per_vertex + k];
						bones.write[v * weights_per_vertex + k] = (b >= 0 && b < (int)bind_bones.size() && bind_bones[b] >= 0) ? bind_bones[b] : 0;
					}
				}
				vertices.write[v] = to_rest.xform(vertices[v]);
				if (v < normals.size()) {
					normals.write[v] = to_rest.basis.xform(normals[v]).normalized();
				}
				if (v * 4 + 3 < tangents.size()) {
					const Vector3 t = to_rest.basis.xform(Vector3(tangents[v * 4], tangents[v * 4 + 1], tangents[v * 4 + 2])).normalized();
					tangents.write[v * 4] = t.x;
					tangents.write[v * 4 + 1] = t.y;
					tangents.write[v * 4 + 2] = t.z;
				}
			}
			arrays[Mesh::ARRAY_VERTEX] = vertices;
			arrays[Mesh::ARRAY_NORMAL] = normals;
			if (!tangents.is_empty()) {
				arrays[Mesh::ARRAY_TANGENT] = tangents;
			}
			arrays[Mesh::ARRAY_BONES] = bones;

			// Material: by the FBX material's name, else by surface order.
			String material_id;
			const Ref<Material> source_material = source_mesh->get_surface_material(s);
			if (source_material.is_valid() && material_by_slot.has(source_material->get_name())) {
				material_id = material_by_slot[source_material->get_name()];
			} else if (material_by_slot.has(source_mesh->get_surface_name(s))) {
				material_id = material_by_slot[source_mesh->get_surface_name(s)];
			} else if (material_by_index.has(s)) {
				material_id = material_by_index[s];
			}
			Ref<Material> material = material_id.is_empty() ? source_material : _material(p_ctx, material_id, source_mesh->get_surface_name(s));
			mesh->add_surface(source_mesh->get_surface_primitive_type(s), arrays, TypedArray<Array>(), Dictionary(), material,
					source_mesh->get_surface_name(s), format & (Mesh::ARRAY_FLAG_USE_8_BONE_WEIGHTS));
		}

		ImporterMeshInstance3D *instance = memnew(ImporterMeshInstance3D);
		instance->set_name(source->get_name());
		instance->set_mesh(mesh);
		instance->set_skin(p_ctx.skin);
		instance->set_skeleton_path(NodePath(".."));
		p_ctx.skeleton->add_child(instance, true);
		instance->set_owner(p_ctx.skeleton->get_owner());
	}
	if (worst_rest_mismatch > 0.01) {
		WARN_PRINT(vformat("MetaHuman: the clothing's skeleton differs from the DNA's by up to %.3f m at rest; it may not sit on the body.", worst_rest_mismatch));
	}
	print_verbose(vformat("MetaHuman: clothing '%s': %d meshes, skeletons agree within %.4f m.", path, instances.size(), worst_rest_mismatch));
	memdelete(root);
}
#endif // MODULE_FBX_ENABLED

String _manifest_dna_path(const Context &p_ctx, const String &p_role) {
	const Dictionary dna = p_ctx.manifest.get("dna", Dictionary());
	const String relative = dna.get(p_role, String(p_role + ".dna"));
	return p_ctx.folder.path_join(relative);
}

} // namespace

void EditorSceneFormatImporterMetaHuman::get_extensions(List<String> *r_extensions) const {
	r_extensions->push_back("dna");
}

void EditorSceneFormatImporterMetaHuman::get_import_options(const String &p_path, List<ResourceImporter::ImportOption> *r_options) {
	if (!p_path.is_empty() && p_path.get_extension().to_lower() != "dna") {
		return;
	}
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "metahuman/include_body"), true));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "metahuman/include_clothing"), true));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "metahuman/hide_skin_under_clothing"), true));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/lods/face_lod_count", PROPERTY_HINT_RANGE, "1,8,1"), 8));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/lods/body_lod_count", PROPERTY_HINT_RANGE, "1,4,1"), 4));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "metahuman/lods/distance_scale", PROPERTY_HINT_RANGE, "0.1,10,0.01"), 1.0));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "metahuman/materials/subsurface_scattering"), true));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "metahuman/materials/skin_detail_size", PROPERTY_HINT_RANGE, "0.002,0.05,0.0005,suffix:m"), 0.012));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/hair/scalp", PROPERTY_HINT_ENUM, "None,Shell,Strands"), SCALP_STRANDS));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "metahuman/hair/scalp_strand_fraction", PROPERTY_HINT_RANGE, "0.02,1,0.01"), 0.2));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/hair/scalp_strand_points", PROPERTY_HINT_RANGE, "4,64,1"), 10));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "metahuman/hair/scalp_strand_distance", PROPERTY_HINT_RANGE, "0.5,20,0.1,suffix:m"), 3.0));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/hair/scalp_strand_coverage", PROPERTY_HINT_ENUM, "Solid,Hashed (needs TAA)"), SCALP_COVERAGE_SOLID));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "metahuman/hair/dynamic"), true));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/hair/guide_points", PROPERTY_HINT_RANGE, "4,64,1"), 16));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/hair/eyebrows", PROPERTY_HINT_ENUM, "None,Strands"), STRANDS_ON));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/hair/eyelashes", PROPERTY_HINT_ENUM, "DNA Cards,Strands"), EYELASHES_STRANDS));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/hair/fuzz", PROPERTY_HINT_ENUM, "None,Strands"), STRANDS_NONE));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "metahuman/hair/strand_fraction", PROPERTY_HINT_RANGE, "0.05,1,0.01"), 1.0));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::FLOAT, "metahuman/hair/shell_voxel_size_mm", PROPERTY_HINT_RANGE, "1,10,0.1"), 3.0));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/hair/shell_triangles", PROPERTY_HINT_RANGE, "500,100000,100"), 8000));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::BOOL, "metahuman/textures/configure_import"), true));
	r_options->push_back(ResourceImporter::ImportOption(PropertyInfo(Variant::INT, "metahuman/textures/max_size", PROPERTY_HINT_ENUM, "No Limit:0,512:512,1024:1024,2048:2048,4096:4096,8192:8192"), 2048));
}

Variant EditorSceneFormatImporterMetaHuman::get_option_visibility(const String &p_path, const String &p_scene_import_type, const String &p_option, const HashMap<StringName, Variant> &p_options) {
	return true;
}

Node *EditorSceneFormatImporterMetaHuman::import_scene(const String &p_path, uint32_t p_flags, const HashMap<StringName, Variant> &p_options, List<String> *r_missing_deps, Error *r_err) {
	Context ctx;
	ctx.folder = p_path.get_base_dir();
	ctx.options = p_options;
	ctx.flags = p_flags;

	const Variant manifest = _load_json(ctx.folder.path_join("CharacterAssemblyManifest.json"));
	if (manifest.get_type() == Variant::DICTIONARY) {
		ctx.manifest = manifest;
		const Array materials = ctx.manifest.get("materials", Array());
		for (int i = 0; i < materials.size(); i++) {
			const Dictionary material = materials[i];
			ctx.material_defs.insert(material.get("name", ""), material);
			ctx.material_by_slot.insert(material.get("slot", ""), material.get("name", ""));
		}
	}

	// Which rigs this file brings in.
	const String file = p_path.get_file().to_lower();
	const String body_path = _manifest_dna_path(ctx, "body");
	const bool is_body = p_path.simplify_path() == body_path.simplify_path() || (file == "body.dna" && manifest.get_type() != Variant::DICTIONARY);
	const bool want_face = !is_body;
	const bool want_body = is_body || (ctx.option("metahuman/include_body", true) && FileAccess::exists(body_path));

	LoadedDNA body_dna;
	LoadedDNA face_dna;
	if (want_body && !_load_dna(is_body ? p_path : body_path, body_dna)) {
		if (r_err) {
			*r_err = ERR_FILE_CORRUPT;
		}
		return nullptr;
	}
	if (want_face && !_load_dna(p_path, face_dna)) {
		if (r_err) {
			*r_err = ERR_FILE_CORRUPT;
		}
		return nullptr;
	}

	Node3D *root = memnew(Node3D);
	root->set_name(String(ctx.manifest.get("name", p_path.get_file().get_basename())).validate_node_name());
	ctx.skeleton = memnew(Skeleton3D);
	ctx.skeleton->set_name("Skeleton3D");
	root->add_child(ctx.skeleton, true);
	ctx.skeleton->set_owner(root);

	// The body first: its joints own every transform the two rigs share.
	if (want_body) {
		_add_joints(ctx, body_dna);
	}
	if (want_face) {
		_add_joints(ctx, face_dna);
	}
	_finish_skeleton(ctx);

	if (want_body) {
		_add_dna_meshes(ctx, body_dna, "body", BODY_LOD_DISTANCES, (int)std::size(BODY_LOD_DISTANCES), ctx.option("metahuman/lods/body_lod_count", 4));
	}
	Vector<GroomSource> grooms;
	if (want_face) {
		grooms = _groom_sources(ctx);
		for (const GroomSource &groom : grooms) {
			if (groom.kind == GROOM_EYELASHES && ctx.option("metahuman/hair/eyelashes", (int)EYELASHES_STRANDS) == EYELASHES_STRANDS) {
				ctx.groom_eyelashes = true;
			}
		}
		_add_dna_meshes(ctx, face_dna, "head", FACE_LOD_DISTANCES, (int)std::size(FACE_LOD_DISTANCES), ctx.option("metahuman/lods/face_lod_count", 8));
		_add_grooms(ctx, grooms);
	}

#ifdef MODULE_FBX_ENABLED
	if (want_body && ctx.option("metahuman/include_clothing", true)) {
		const Array components = ctx.manifest.get("components", Array());
		for (int i = 0; i < components.size(); i++) {
			const Dictionary component = components[i];
			if (String(component.get("type", "")) == "fbx" && String(component.get("role", "")) == "clothing") {
				_add_clothing(ctx, component);
			}
		}
	}
#endif

	MetaHumanRig *rig = memnew(MetaHumanRig);
	rig->set_name("MetaHumanRig");
	const float distance_scale = ctx.option("metahuman/lods/distance_scale", 1.0f);
	if (want_body) {
		rig->set_body_dna(body_dna.runtime);
		PackedFloat32Array distances;
		for (float d : BODY_LOD_DISTANCES) {
			distances.push_back(d * distance_scale);
		}
		rig->set_body_lod_distances(distances);
	}
	if (want_face) {
		rig->set_face_dna(face_dna.runtime);
		PackedFloat32Array distances;
		for (float d : FACE_LOD_DISTANCES) {
			distances.push_back(d * distance_scale);
		}
		rig->set_face_lod_distances(distances);
	}
	ctx.skeleton->add_child(rig, true);
	rig->set_owner(root);
	for (Node *modifier : ctx.pending_modifiers) {
		ctx.skeleton->add_child(modifier, true);
		modifier->set_owner(root);
	}

	if (r_err) {
		*r_err = OK;
	}
	return root;
}
