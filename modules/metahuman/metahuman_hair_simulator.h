/**************************************************************************/
/*  metahuman_hair_simulator.h                                            */
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

#include "core/io/resource.h"
#include "core/templates/local_vector.h"
#include "scene/3d/skeleton_modifier_3d.h"
#include "scene/resources/image_texture.h"

class GeometryInstance3D;
class Image;

// The guide curves of a groom, as the simulator needs them: their rest shape in skeleton space
// (point_count points each), the skin binding of their roots, and the colliders they keep out of.
class MetaHumanHairSimulation : public Resource {
	GDCLASS(MetaHumanHairSimulation, Resource);

	int point_count = 16;
	PackedVector3Array rest_points;
	PackedInt32Array root_bones; // 4 per guide.
	PackedFloat32Array root_weights; // 4 per guide.
	PackedInt32Array collider_bones;
	PackedVector3Array collider_offsets; // In the bone's space.
	PackedFloat32Array collider_radii;

protected:
	static void _bind_methods();

public:
	void set_point_count(int p_count);
	int get_point_count() const;
	int get_guide_count() const;
	void set_rest_points(const PackedVector3Array &p_points);
	PackedVector3Array get_rest_points() const;
	void set_root_bones(const PackedInt32Array &p_bones);
	PackedInt32Array get_root_bones() const;
	void set_root_weights(const PackedFloat32Array &p_weights);
	PackedFloat32Array get_root_weights() const;
	void set_collider_bones(const PackedInt32Array &p_bones);
	PackedInt32Array get_collider_bones() const;
	void set_collider_offsets(const PackedVector3Array &p_offsets);
	PackedVector3Array get_collider_offsets() const;
	void set_collider_radii(const PackedFloat32Array &p_radii);
	PackedFloat32Array get_collider_radii() const;
};

// Simulates a groom's guides every frame and hands the hair meshes how far each guide point moved
// from where skinning alone puts it, as a texture their vertex shaders read (guide_offsets: one row
// per guide, one texel per point). Every rendered strand and shell vertex follows its two guides,
// so the simulation costs ~20 k particles however dense the hair is drawn.
//
// The solver is position-based: Verlet integration with damping, segment lengths kept, a shape
// constraint that pulls toward the groomed rest pose (stiff at the root, loose at the tip, so the
// style holds while the ends swing), and sphere colliders on bones.
class MetaHumanHairSimulator : public SkeletonModifier3D {
	GDCLASS(MetaHumanHairSimulator, SkeletonModifier3D);

	Ref<MetaHumanHairSimulation> simulation;
	TypedArray<NodePath> hair_meshes;
	bool simulating = true;
	Vector3 gravity = Vector3(0, -9.8, 0);
	Vector3 wind;
	float damping = 0.1;
	float root_stiffness = 0.6;
	float tip_stiffness = 0.02;
	// Solver steps per 1/60 s (a fixed step of 1 / (60 * substeps) seconds).
	int substeps = 2;
	int iterations = 3;

	LocalVector<Vector3> positions; // World space.
	LocalVector<Vector3> previous;
	LocalVector<Vector3> targets;
	LocalVector<float> segment_lengths;
	LocalVector<Transform3D> guide_transforms; // Skeleton space, from the root binding.
	LocalVector<Transform3D> bone_rest_inverse;
	// Where each point hangs at rest under gravity, in its guide's frame. The groom was styled under
	// gravity already, so offsets are reported from there: idle hair keeps its groomed shape.
	LocalVector<Vector3> equilibrium;
	bool initialized = false;
	float accumulated = 0.0;
	Ref<Image> offset_image;
	Ref<ImageTexture> offset_texture;
	bool materials_bound = false;
	float max_offset = 0.0;
	float mean_offset = 0.0;

	void _reset_state();
	void _step(float p_step, const LocalVector<Vector3> &p_centers, const LocalVector<float> &p_radii);
	void _colliders(Skeleton3D *p_skeleton, const Transform3D &p_skeleton_global, LocalVector<Vector3> &r_centers, LocalVector<float> &r_radii) const;
	void _bind_materials();
	void _update_texture(const Transform3D &p_skeleton_inverse);

protected:
	static void _bind_methods();
	void _notification(int p_what);
	virtual void _process_modification(double p_delta) override;

public:
	virtual bool has_process() const override { return true; }

	void set_simulation(const Ref<MetaHumanHairSimulation> &p_simulation);
	Ref<MetaHumanHairSimulation> get_simulation() const;
	void set_hair_meshes(const TypedArray<NodePath> &p_meshes);
	TypedArray<NodePath> get_hair_meshes() const;
	void set_simulating(bool p_simulating);
	bool is_simulating() const;
	void set_gravity(const Vector3 &p_gravity);
	Vector3 get_gravity() const;
	void set_wind(const Vector3 &p_wind);
	Vector3 get_wind() const;
	void set_damping(float p_damping);
	float get_damping() const;
	void set_root_stiffness(float p_stiffness);
	float get_root_stiffness() const;
	void set_tip_stiffness(float p_stiffness);
	float get_tip_stiffness() const;
	void set_substeps(int p_substeps);
	int get_substeps() const;
	void set_iterations(int p_iterations);
	int get_iterations() const;

	// Snaps the hair back to its rest pose (after a teleport, say).
	void reset();
	// The largest distance a guide point is from where skinning alone puts it, this frame (meters).
	float get_max_offset() const;
	// The mean of the same over all guide points.
	float get_mean_offset() const;
};
