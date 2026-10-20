/**************************************************************************/
/*  metahuman_hair_simulator.cpp                                          */
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

#include "metahuman_hair_simulator.h"

#include "core/io/image.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/material.h"

// Simulation data ----------------------------------------------------------------------------------

void MetaHumanHairSimulation::set_point_count(int p_count) {
	point_count = MAX(p_count, 2);
	emit_changed();
}

int MetaHumanHairSimulation::get_point_count() const {
	return point_count;
}

int MetaHumanHairSimulation::get_guide_count() const {
	return rest_points.size() / point_count;
}

void MetaHumanHairSimulation::set_rest_points(const PackedVector3Array &p_points) {
	rest_points = p_points;
	emit_changed();
}

PackedVector3Array MetaHumanHairSimulation::get_rest_points() const {
	return rest_points;
}

void MetaHumanHairSimulation::set_root_bones(const PackedInt32Array &p_bones) {
	root_bones = p_bones;
}

PackedInt32Array MetaHumanHairSimulation::get_root_bones() const {
	return root_bones;
}

void MetaHumanHairSimulation::set_root_weights(const PackedFloat32Array &p_weights) {
	root_weights = p_weights;
}

PackedFloat32Array MetaHumanHairSimulation::get_root_weights() const {
	return root_weights;
}

void MetaHumanHairSimulation::set_collider_bones(const PackedInt32Array &p_bones) {
	collider_bones = p_bones;
}

PackedInt32Array MetaHumanHairSimulation::get_collider_bones() const {
	return collider_bones;
}

void MetaHumanHairSimulation::set_collider_offsets(const PackedVector3Array &p_offsets) {
	collider_offsets = p_offsets;
}

PackedVector3Array MetaHumanHairSimulation::get_collider_offsets() const {
	return collider_offsets;
}

void MetaHumanHairSimulation::set_collider_radii(const PackedFloat32Array &p_radii) {
	collider_radii = p_radii;
}

PackedFloat32Array MetaHumanHairSimulation::get_collider_radii() const {
	return collider_radii;
}

void MetaHumanHairSimulation::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_point_count", "count"), &MetaHumanHairSimulation::set_point_count);
	ClassDB::bind_method(D_METHOD("get_point_count"), &MetaHumanHairSimulation::get_point_count);
	ClassDB::bind_method(D_METHOD("get_guide_count"), &MetaHumanHairSimulation::get_guide_count);
	ClassDB::bind_method(D_METHOD("set_rest_points", "points"), &MetaHumanHairSimulation::set_rest_points);
	ClassDB::bind_method(D_METHOD("get_rest_points"), &MetaHumanHairSimulation::get_rest_points);
	ClassDB::bind_method(D_METHOD("set_root_bones", "bones"), &MetaHumanHairSimulation::set_root_bones);
	ClassDB::bind_method(D_METHOD("get_root_bones"), &MetaHumanHairSimulation::get_root_bones);
	ClassDB::bind_method(D_METHOD("set_root_weights", "weights"), &MetaHumanHairSimulation::set_root_weights);
	ClassDB::bind_method(D_METHOD("get_root_weights"), &MetaHumanHairSimulation::get_root_weights);
	ClassDB::bind_method(D_METHOD("set_collider_bones", "bones"), &MetaHumanHairSimulation::set_collider_bones);
	ClassDB::bind_method(D_METHOD("get_collider_bones"), &MetaHumanHairSimulation::get_collider_bones);
	ClassDB::bind_method(D_METHOD("set_collider_offsets", "offsets"), &MetaHumanHairSimulation::set_collider_offsets);
	ClassDB::bind_method(D_METHOD("get_collider_offsets"), &MetaHumanHairSimulation::get_collider_offsets);
	ClassDB::bind_method(D_METHOD("set_collider_radii", "radii"), &MetaHumanHairSimulation::set_collider_radii);
	ClassDB::bind_method(D_METHOD("get_collider_radii"), &MetaHumanHairSimulation::get_collider_radii);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "point_count", PROPERTY_HINT_RANGE, "2,64,1"), "set_point_count", "get_point_count");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "rest_points", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_rest_points", "get_rest_points");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "root_bones", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_root_bones", "get_root_bones");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "root_weights", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_root_weights", "get_root_weights");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "collider_bones"), "set_collider_bones", "get_collider_bones");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "collider_offsets"), "set_collider_offsets", "get_collider_offsets");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "collider_radii", PROPERTY_HINT_NONE, "suffix:m"), "set_collider_radii", "get_collider_radii");
}

// Simulator ------------------------------------------------------------------------------------------

void MetaHumanHairSimulator::_reset_state() {
	initialized = false;
	materials_bound = false;
	positions.clear();
	previous.clear();
	targets.clear();
}

void MetaHumanHairSimulator::reset() {
	initialized = false;
}

float MetaHumanHairSimulator::get_max_offset() const {
	return max_offset;
}

float MetaHumanHairSimulator::get_mean_offset() const {
	return mean_offset;
}

void MetaHumanHairSimulator::_bind_materials() {
	materials_bound = true;
	if (simulation.is_null()) {
		return;
	}
	// Rows padded to 32 texels (256 bytes of RGBA16F): WebGPU copies need 256-byte row pitches, and
	// narrower rows are uploaded in many pieces.
	const int width = ((MAX(simulation->get_point_count(), 1) + 31) / 32) * 32;
	const int height = MAX(simulation->get_guide_count(), 1);
	offset_image = Image::create_empty(width, height, false, Image::FORMAT_RGBAH);
	offset_image->fill(Color(0, 0, 0, 0));
	offset_texture = ImageTexture::create_from_image(offset_image);

	// Each mesh gets its own copies of its materials, so two characters sharing the imported
	// scene do not share one offset texture.
	for (int i = 0; i < hair_meshes.size(); i++) {
		MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(get_node_or_null(hair_meshes[i]));
		if (!mesh_instance || mesh_instance->get_mesh().is_null()) {
			continue;
		}
		for (int s = 0; s < mesh_instance->get_mesh()->get_surface_count(); s++) {
			Ref<ShaderMaterial> material = mesh_instance->get_active_material(s);
			if (material.is_null()) {
				continue;
			}
			Ref<ShaderMaterial> own = material->duplicate();
			own->set_shader_parameter("guide_offsets", offset_texture);
			own->set_shader_parameter("guide_point_count", simulation->get_point_count());
			mesh_instance->set_surface_override_material(s, own);
		}
	}
}

void MetaHumanHairSimulator::_update_texture(const Transform3D &p_skeleton_inverse) {
	const int point_count = simulation->get_point_count();
	const int guide_count = simulation->get_guide_count();
	const int width = offset_image->get_width();
	PackedByteArray bytes;
	bytes.resize(width * guide_count * 8);
	memset(bytes.ptrw(), 0, bytes.size());
	uint16_t *texels = reinterpret_cast<uint16_t *>(bytes.ptrw());
	max_offset = 0.0;
	mean_offset = 0.0;
	for (int i = 0; i < point_count * guide_count; i++) {
		const int texel = (i / point_count) * width + i % point_count;
		// The offset in skeleton space: that is the space the meshes' VERTEX is in.
		const Vector3 settled = targets[i] + guide_transforms[i / point_count].basis.xform(equilibrium[i]);
		const Vector3 offset = p_skeleton_inverse.basis.xform(positions[i] - settled);
		max_offset = MAX(max_offset, (float)offset.length());
		mean_offset += offset.length() / (point_count * guide_count);
		texels[texel * 4] = Math::make_half_float(offset.x);
		texels[texel * 4 + 1] = Math::make_half_float(offset.y);
		texels[texel * 4 + 2] = Math::make_half_float(offset.z);
	}
	offset_image->set_data(width, guide_count, false, Image::FORMAT_RGBAH, bytes);
	offset_texture->update(offset_image);
}

void MetaHumanHairSimulator::_colliders(Skeleton3D *p_skeleton, const Transform3D &p_skeleton_global, LocalVector<Vector3> &r_centers, LocalVector<float> &r_radii) const {
	const PackedInt32Array collider_bones = simulation->get_collider_bones();
	const PackedVector3Array collider_offsets = simulation->get_collider_offsets();
	const PackedFloat32Array collider_radii = simulation->get_collider_radii();
	const int bone_count = p_skeleton->get_bone_count();
	for (int c = 0; c < collider_bones.size() && c < collider_offsets.size() && c < collider_radii.size(); c++) {
		const int bone = collider_bones[c];
		if (bone < 0 || bone >= bone_count) {
			continue;
		}
		r_centers.push_back(p_skeleton_global.xform(p_skeleton->get_bone_global_pose(bone).xform(collider_offsets[c])));
		r_radii.push_back(collider_radii[c]);
	}
}

void MetaHumanHairSimulator::_step(float p_step, const LocalVector<Vector3> &p_centers, const LocalVector<float> &p_radii) {
	const int point_count = simulation->get_point_count();
	const int total = positions.size();
	const int guide_count = total / point_count;
	const Vector3 acceleration = (gravity + wind) * p_step * p_step;
	const float keep = 1.0f - CLAMP(damping, 0.0f, 1.0f);
	for (int i = 0; i < total; i++) {
		if (i % point_count == 0) {
			positions[i] = targets[i];
			previous[i] = targets[i];
			continue;
		}
		const Vector3 velocity = (positions[i] - previous[i]) * keep;
		previous[i] = positions[i];
		positions[i] += velocity + acceleration;
	}
	for (int it = 0; it < iterations; it++) {
		for (int g = 0; g < guide_count; g++) {
			const int base = g * point_count;
			for (int p = 1; p < point_count; p++) {
				Vector3 &a = positions[base + p - 1];
				Vector3 &b = positions[base + p];
				// Keep the segment's rest length; the root end stays put.
				const Vector3 d = b - a;
				const float length = d.length();
				if (length > 1e-7f) {
					const Vector3 correction = d * ((length - segment_lengths[base + p]) / length);
					if (p == 1) {
						b -= correction;
					} else {
						a += correction * 0.5f;
						b -= correction * 0.5f;
					}
				}
				// Pull toward the groomed shape: stiff near the root, loose at the tip.
				const float t = float(p) / (point_count - 1);
				const float stiffness = Math::lerp(root_stiffness, tip_stiffness, Math::sqrt(t));
				b += (targets[base + p] - b) * (stiffness / iterations);
				// The root part lies on the scalp, inside the skull's sphere; colliding it would fight
				// the shape constraint every iteration and never settle.
				if (t < 0.2f) {
					continue;
				}
				for (uint32_t c = 0; c < p_centers.size(); c++) {
					const Vector3 offset = b - p_centers[c];
					const float distance_squared = offset.length_squared();
					if (distance_squared < p_radii[c] * p_radii[c] && distance_squared > 1e-12f) {
						b = p_centers[c] + offset * (p_radii[c] / Math::sqrt(distance_squared));
					}
				}
			}
		}
	}
}

void MetaHumanHairSimulator::_process_modification(double p_delta) {
	Skeleton3D *skeleton = get_skeleton();
	if (!skeleton || simulation.is_null() || simulation->get_guide_count() == 0) {
		return;
	}
	if (!materials_bound) {
		_bind_materials();
	}
	const int point_count = simulation->get_point_count();
	const int guide_count = simulation->get_guide_count();
	const int total = point_count * guide_count;
	const PackedVector3Array rest = simulation->get_rest_points();
	const PackedInt32Array bones = simulation->get_root_bones();
	const PackedFloat32Array weights = simulation->get_root_weights();
	const int bone_count = skeleton->get_bone_count();

	if (bone_rest_inverse.size() != (uint32_t)bone_count) {
		bone_rest_inverse.resize(bone_count);
		for (int b = 0; b < bone_count; b++) {
			bone_rest_inverse[b] = skeleton->get_bone_global_rest(b).affine_inverse();
		}
	}

	// Where skinning alone puts each guide: its root binding's blend of bone transforms.
	const Transform3D skeleton_global = skeleton->get_global_transform();
	guide_transforms.resize(guide_count);
	targets.resize(total);
	for (int g = 0; g < guide_count; g++) {
		Transform3D blend;
		blend.basis = Basis(Vector3(), Vector3(), Vector3());
		float total_weight = 0.0;
		for (int k = 0; k < 4; k++) {
			const int bone = g * 4 + k < bones.size() ? bones[g * 4 + k] : -1;
			const float w = g * 4 + k < weights.size() ? weights[g * 4 + k] : 0.0f;
			if (bone < 0 || bone >= bone_count || w <= 0.0f) {
				continue;
			}
			const Transform3D m = skeleton->get_bone_global_pose(bone) * bone_rest_inverse[bone];
			blend.basis.rows[0] += m.basis.rows[0] * w;
			blend.basis.rows[1] += m.basis.rows[1] * w;
			blend.basis.rows[2] += m.basis.rows[2] * w;
			blend.origin += m.origin * w;
			total_weight += w;
		}
		if (total_weight <= 0.0f) {
			blend = Transform3D();
		}
		guide_transforms[g] = skeleton_global * blend;
		for (int p = 0; p < point_count; p++) {
			targets[g * point_count + p] = guide_transforms[g].xform(rest[g * point_count + p]);
		}
	}

	LocalVector<Vector3> centers;
	LocalVector<float> radii;
	_colliders(skeleton, skeleton_global, centers, radii);

	if (!initialized || positions.size() != (uint32_t)total) {
		positions.resize(total);
		previous.resize(total);
		segment_lengths.resize(total);
		equilibrium.resize(total);
		for (int i = 0; i < total; i++) {
			positions[i] = targets[i];
			previous[i] = targets[i];
			segment_lengths[i] = (i % point_count) == 0 ? 0.0f : rest[i].distance_to(rest[i - 1]);
		}
		// Settle under gravity in the current pose and remember where the points came to rest.
		const float fixed_step = 1.0f / (60.0f * MAX(substeps, 1));
		for (int i = 0; i < 400; i++) {
			_step(fixed_step, centers, radii);
		}
		accumulated = 0.0;
		for (int i = 0; i < total; i++) {
			equilibrium[i] = guide_transforms[i / point_count].basis.inverse().xform(positions[i] - targets[i]);
		}
		initialized = true;
	}

	if (simulating) {
		// A fixed step: the solver's rest sag depends on the step size (gravity enters as step²), so a
		// varying one would drift from the equilibrium measured above and behave differently per frame
		// rate. Long hitches (a load, a breakpoint) are dropped rather than caught up.
		const float fixed_step = 1.0f / (60.0f * MAX(substeps, 1));
		accumulated = MIN(accumulated + (float)p_delta, fixed_step * substeps * 2.0f);
		while (accumulated >= fixed_step) {
			_step(fixed_step, centers, radii);
			accumulated -= fixed_step;
		}
	} else {
		for (int i = 0; i < total; i++) {
			positions[i] = targets[i] + guide_transforms[i / point_count].basis.xform(equilibrium[i]);
			previous[i] = positions[i];
		}
	}

	_update_texture(skeleton_global.affine_inverse());
}

void MetaHumanHairSimulator::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		_reset_state();
	}
}

void MetaHumanHairSimulator::set_simulation(const Ref<MetaHumanHairSimulation> &p_simulation) {
	simulation = p_simulation;
	_reset_state();
}

Ref<MetaHumanHairSimulation> MetaHumanHairSimulator::get_simulation() const {
	return simulation;
}

void MetaHumanHairSimulator::set_hair_meshes(const TypedArray<NodePath> &p_meshes) {
	hair_meshes = p_meshes;
	materials_bound = false;
}

TypedArray<NodePath> MetaHumanHairSimulator::get_hair_meshes() const {
	return hair_meshes;
}

void MetaHumanHairSimulator::set_simulating(bool p_simulating) {
	simulating = p_simulating;
}

bool MetaHumanHairSimulator::is_simulating() const {
	return simulating;
}

void MetaHumanHairSimulator::set_gravity(const Vector3 &p_gravity) {
	gravity = p_gravity;
}

Vector3 MetaHumanHairSimulator::get_gravity() const {
	return gravity;
}

void MetaHumanHairSimulator::set_wind(const Vector3 &p_wind) {
	wind = p_wind;
}

Vector3 MetaHumanHairSimulator::get_wind() const {
	return wind;
}

void MetaHumanHairSimulator::set_damping(float p_damping) {
	damping = p_damping;
}

float MetaHumanHairSimulator::get_damping() const {
	return damping;
}

void MetaHumanHairSimulator::set_root_stiffness(float p_stiffness) {
	root_stiffness = p_stiffness;
}

float MetaHumanHairSimulator::get_root_stiffness() const {
	return root_stiffness;
}

void MetaHumanHairSimulator::set_tip_stiffness(float p_stiffness) {
	tip_stiffness = p_stiffness;
}

float MetaHumanHairSimulator::get_tip_stiffness() const {
	return tip_stiffness;
}

void MetaHumanHairSimulator::set_substeps(int p_substeps) {
	substeps = CLAMP(p_substeps, 1, 16);
}

int MetaHumanHairSimulator::get_substeps() const {
	return substeps;
}

void MetaHumanHairSimulator::set_iterations(int p_iterations) {
	iterations = CLAMP(p_iterations, 1, 16);
}

int MetaHumanHairSimulator::get_iterations() const {
	return iterations;
}

void MetaHumanHairSimulator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_simulation", "simulation"), &MetaHumanHairSimulator::set_simulation);
	ClassDB::bind_method(D_METHOD("get_simulation"), &MetaHumanHairSimulator::get_simulation);
	ClassDB::bind_method(D_METHOD("set_hair_meshes", "meshes"), &MetaHumanHairSimulator::set_hair_meshes);
	ClassDB::bind_method(D_METHOD("get_hair_meshes"), &MetaHumanHairSimulator::get_hair_meshes);
	ClassDB::bind_method(D_METHOD("set_simulating", "simulating"), &MetaHumanHairSimulator::set_simulating);
	ClassDB::bind_method(D_METHOD("is_simulating"), &MetaHumanHairSimulator::is_simulating);
	ClassDB::bind_method(D_METHOD("set_gravity", "gravity"), &MetaHumanHairSimulator::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &MetaHumanHairSimulator::get_gravity);
	ClassDB::bind_method(D_METHOD("set_wind", "wind"), &MetaHumanHairSimulator::set_wind);
	ClassDB::bind_method(D_METHOD("get_wind"), &MetaHumanHairSimulator::get_wind);
	ClassDB::bind_method(D_METHOD("set_damping", "damping"), &MetaHumanHairSimulator::set_damping);
	ClassDB::bind_method(D_METHOD("get_damping"), &MetaHumanHairSimulator::get_damping);
	ClassDB::bind_method(D_METHOD("set_root_stiffness", "stiffness"), &MetaHumanHairSimulator::set_root_stiffness);
	ClassDB::bind_method(D_METHOD("get_root_stiffness"), &MetaHumanHairSimulator::get_root_stiffness);
	ClassDB::bind_method(D_METHOD("set_tip_stiffness", "stiffness"), &MetaHumanHairSimulator::set_tip_stiffness);
	ClassDB::bind_method(D_METHOD("get_tip_stiffness"), &MetaHumanHairSimulator::get_tip_stiffness);
	ClassDB::bind_method(D_METHOD("set_substeps", "substeps"), &MetaHumanHairSimulator::set_substeps);
	ClassDB::bind_method(D_METHOD("get_substeps"), &MetaHumanHairSimulator::get_substeps);
	ClassDB::bind_method(D_METHOD("set_iterations", "iterations"), &MetaHumanHairSimulator::set_iterations);
	ClassDB::bind_method(D_METHOD("get_iterations"), &MetaHumanHairSimulator::get_iterations);
	ClassDB::bind_method(D_METHOD("reset"), &MetaHumanHairSimulator::reset);
	ClassDB::bind_method(D_METHOD("get_max_offset"), &MetaHumanHairSimulator::get_max_offset);
	ClassDB::bind_method(D_METHOD("get_mean_offset"), &MetaHumanHairSimulator::get_mean_offset);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "simulation", PROPERTY_HINT_RESOURCE_TYPE, MetaHumanHairSimulation::get_class_static()), "set_simulation", "get_simulation");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "hair_meshes", PROPERTY_HINT_ARRAY_TYPE, "NodePath"), "set_hair_meshes", "get_hair_meshes");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulating"), "set_simulating", "is_simulating");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "gravity", PROPERTY_HINT_NONE, "suffix:m/s²"), "set_gravity", "get_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind", PROPERTY_HINT_NONE, "suffix:m/s²"), "set_wind", "get_wind");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_damping", "get_damping");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "root_stiffness", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_root_stiffness", "get_root_stiffness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tip_stiffness", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_tip_stiffness", "get_tip_stiffness");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "substeps", PROPERTY_HINT_RANGE, "1,16,1"), "set_substeps", "get_substeps");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "iterations", PROPERTY_HINT_RANGE, "1,16,1"), "set_iterations", "get_iterations");
}
