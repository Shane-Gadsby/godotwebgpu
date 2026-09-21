/**************************************************************************/
/*  box3d_shape_3d.h                                                      */
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

#include "../misc/box3d_common.h"
#include "../spaces/box3d_query_shape.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "servers/physics_3d/physics_server_3d.h"

class Box3DObject3D;
class Box3DShape3D;

// Data owned by a shape instance that has to outlive the Box3D shape it was used to create.
struct Box3DOwnedData {
	enum Type {
		MESH,
		HEIGHT_FIELD,
	};

	Type type = MESH;
	void *data = nullptr;
};

// A Godot shape attached to one physical object. Box3D shapes belong to a single body and have their transform baked
// into their geometry, so every (object, shape) pair gets its own instance holding the Box3D shape built for it.
class Box3DShapeInstance {
	inline static uint32_t next_id = 1;

	Box3DObject3D *owner = nullptr;
	Box3DShape3D *shape = nullptr;
	Transform3D transform;
	uint32_t id = next_id++;
	bool disabled = false;

public:
	LocalVector<b3ShapeId> built_shapes;
	LocalVector<Box3DOwnedData> owned_data;

	Box3DShapeInstance(Box3DObject3D *p_owner, Box3DShape3D *p_shape, const Transform3D &p_transform, bool p_disabled);
	~Box3DShapeInstance();

	uint32_t get_id() const { return id; }
	Box3DObject3D *get_owner() const { return owner; }
	Box3DShape3D *get_shape() const { return shape; }
	void set_shape(Box3DShape3D *p_shape);

	const Transform3D &get_transform_unscaled() const { return transform; }
	Transform3D get_transform_scaled(const Vector3 &p_scale) const { return transform.scaled_local(p_scale); }
	void set_transform(const Transform3D &p_transform) { transform = p_transform; }

	bool is_disabled() const { return disabled; }
	void set_disabled(bool p_disabled) { disabled = p_disabled; }

	bool is_built() const { return !built_shapes.is_empty(); }

	AABB get_aabb(const Vector3 &p_scale) const;

	// Builds the Box3D shape(s) on `p_body`. Returns false if the shape could not be built.
	bool build(b3BodyId p_body, b3ShapeDef p_def, const Vector3 &p_scale, bool p_body_is_static);

	// Destroys the Box3D shape(s) built on a body that is still alive.
	void destroy_built();

	// Forgets the Box3D shape(s) without destroying them, for use when the body itself is destroyed.
	void forget_built();
};

class Box3DShape3D {
protected:
	HashMap<Box3DObject3D *, int> ref_counts_by_owner;
	RID rid;

	String _owners_to_string() const;

	bool _register_shape(Box3DShapeInstance &p_instance, b3ShapeId p_id) const;

	// Bakes a hull into a new one that is placed with `p_xform` (scale included). The result is owned by the caller.
	static b3HullData *_bake_hull(const b3HullData *p_hull, const Transform3D &p_xform);
	static bool _make_hull_query_shape(const b3HullData *p_hull, const Transform3D &p_xform, Box3DQueryShape &r_shape);

	// Builds a shape from a hull in its own space, placed with `p_xform` (scale included).
	bool _build_hull(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const b3HullData *p_hull, const Transform3D &p_xform) const;

public:
	typedef PhysicsServer3D::ShapeType ShapeType;

	virtual ~Box3DShape3D() = 0;

	RID get_rid() const { return rid; }
	void set_rid(const RID &p_rid) { rid = p_rid; }

	void add_owner(Box3DObject3D *p_owner);
	void remove_owner(Box3DObject3D *p_owner);
	void remove_self();

	virtual ShapeType get_type() const = 0;
	virtual bool is_convex() const = 0;

	virtual Variant get_data() const = 0;
	virtual void set_data(const Variant &p_data) = 0;

	virtual float get_margin() const { return 0.0f; }
	virtual void set_margin(float p_margin) {}

	virtual AABB get_aabb() const = 0;

	float get_solver_bias() const;
	void set_solver_bias(float p_bias);

	// Creates the Box3D shape(s) for this shape on `p_body`. `p_xform` maps from the shape's space into the body's
	// space, scale included, and `p_def` already has its user data and filter filled in.
	virtual bool build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const = 0;

	// Builds the shape as a query shape placed with `p_xform`. Only convex shapes can be used to query the world.
	virtual bool make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const { return false; }

	// Called when the data of the shape has changed and every object using it has to rebuild.
	void destroy();

	virtual String to_string() const = 0;
};

class Box3DWorldBoundaryShape3D final : public Box3DShape3D {
	Plane plane;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_WORLD_BOUNDARY; }
	virtual bool is_convex() const override { return false; }

	virtual Variant get_data() const override { return plane; }
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override;

	const Plane &get_plane() const { return plane; }

	virtual bool build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const override;

	virtual String to_string() const override;
};

class Box3DSeparationRayShape3D final : public Box3DShape3D {
	float length = 0.0f;
	bool slide_on_slope = false;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_SEPARATION_RAY; }
	virtual bool is_convex() const override { return true; }

	virtual Variant get_data() const override;
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override;

	float get_length() const { return length; }
	bool get_slide_on_slope() const { return slide_on_slope; }

	// Separation rays only take part in motion queries, so no Box3D shape is ever built for them.
	virtual bool build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const override { return true; }

	virtual String to_string() const override;
};

class Box3DSphereShape3D final : public Box3DShape3D {
	float radius = 0.0f;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_SPHERE; }
	virtual bool is_convex() const override { return true; }

	virtual Variant get_data() const override { return radius; }
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override;

	float get_radius() const { return radius; }

	virtual bool build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const override;
	virtual bool make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const override;

	virtual String to_string() const override;
};

class Box3DBoxShape3D final : public Box3DShape3D {
	Vector3 half_extents;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_BOX; }
	virtual bool is_convex() const override { return true; }

	virtual Variant get_data() const override { return half_extents; }
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override { return AABB(-half_extents, half_extents * 2.0f); }

	const Vector3 &get_half_extents() const { return half_extents; }

	virtual bool build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const override;
	virtual bool make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const override;

	virtual String to_string() const override;
};

class Box3DCapsuleShape3D final : public Box3DShape3D {
	float height = 0.0f;
	float radius = 0.0f;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_CAPSULE; }
	virtual bool is_convex() const override { return true; }

	virtual Variant get_data() const override;
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override;

	float get_height() const { return height; }
	float get_radius() const { return radius; }

	virtual bool build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const override;
	virtual bool make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const override;

	virtual String to_string() const override;
};

// A hull-based shape. Box3D has no cylinder primitive, so cylinders are tessellated into a prism.
class Box3DHullShape3D : public Box3DShape3D {
protected:
	mutable b3HullData *hull = nullptr;

	virtual b3HullData *_create_hull() const = 0;

	b3HullData *_get_hull() const;
	void _reset_hull();

public:
	virtual ~Box3DHullShape3D() override;

	virtual bool is_convex() const override { return true; }

	virtual bool build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const override;
	virtual bool make_query_shape(const Transform3D &p_xform, Box3DQueryShape &r_shape) const override;
};

class Box3DCylinderShape3D final : public Box3DHullShape3D {
	float height = 0.0f;
	float radius = 0.0f;

	virtual b3HullData *_create_hull() const override;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_CYLINDER; }

	virtual Variant get_data() const override;
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override;

	float get_height() const { return height; }
	float get_radius() const { return radius; }

	virtual String to_string() const override;
};

class Box3DConvexPolygonShape3D final : public Box3DHullShape3D {
	Vector<Vector3> vertices;
	AABB aabb;

	virtual b3HullData *_create_hull() const override;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_CONVEX_POLYGON; }

	virtual Variant get_data() const override { return vertices; }
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override { return aabb; }

	virtual String to_string() const override;
};

// Triangle soups. Box3D only lets these collide as static geometry.
class Box3DMeshShape3D : public Box3DShape3D {
protected:
	mutable b3MeshData *shared_mesh = nullptr;

	bool _build_mesh_shape(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static, const char *p_kind) const;

	// Builds mesh data for the shape with `p_xform` baked into its vertices.
	virtual b3MeshData *_create_mesh(const Transform3D &p_xform) const = 0;

	void _reset_mesh();

public:
	virtual ~Box3DMeshShape3D() override;

	virtual bool is_convex() const override { return false; }

	virtual bool build(Box3DShapeInstance &p_instance, b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_xform, bool p_body_is_static) const override;
};

class Box3DConcavePolygonShape3D final : public Box3DMeshShape3D {
	Vector<Vector3> faces;
	bool back_face_collision = false;
	AABB aabb;

	virtual b3MeshData *_create_mesh(const Transform3D &p_xform) const override;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_CONCAVE_POLYGON; }

	virtual Variant get_data() const override;
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override { return aabb; }

	const Vector<Vector3> &get_faces() const { return faces; }

	virtual String to_string() const override;
};

class Box3DHeightMapShape3D final : public Box3DMeshShape3D {
	Vector<real_t> heights;
	int width = 0;
	int depth = 0;
	AABB aabb;

	virtual b3MeshData *_create_mesh(const Transform3D &p_xform) const override;

	AABB _calculate_aabb() const;

public:
	virtual ShapeType get_type() const override { return PhysicsServer3D::SHAPE_HEIGHTMAP; }

	virtual Variant get_data() const override;
	virtual void set_data(const Variant &p_data) override;

	virtual AABB get_aabb() const override { return aabb; }

	virtual String to_string() const override;
};
