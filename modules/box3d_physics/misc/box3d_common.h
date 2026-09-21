/**************************************************************************/
/*  box3d_common.h                                                        */
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

#include "core/math/aabb.h"
#include "core/math/basis.h"
#include "core/math/plane.h"
#include "core/math/quaternion.h"
#include "core/math/transform_3d.h"
#include "core/math/vector3.h"
#include "core/string/ustring.h"

#include <box3d/box3d.h>

_FORCE_INLINE_ b3Vec3 to_b3(const Vector3 &p_vec) {
	return b3Vec3{ (float)p_vec.x, (float)p_vec.y, (float)p_vec.z };
}

_FORCE_INLINE_ Vector3 to_godot(const b3Vec3 &p_vec) {
	return Vector3((real_t)p_vec.x, (real_t)p_vec.y, (real_t)p_vec.z);
}

// `b3Pos` is the same type as `b3Vec3` in single-precision builds, so positions get their own names.
_FORCE_INLINE_ b3Pos to_b3_pos(const Vector3 &p_vec) {
	b3Pos pos;
	pos.x = p_vec.x;
	pos.y = p_vec.y;
	pos.z = p_vec.z;
	return pos;
}

_FORCE_INLINE_ Vector3 pos_to_godot(const b3Pos &p_pos) {
	return Vector3((real_t)p_pos.x, (real_t)p_pos.y, (real_t)p_pos.z);
}

_FORCE_INLINE_ b3Quat to_b3(const Quaternion &p_quat) {
	return b3Quat{ b3Vec3{ (float)p_quat.x, (float)p_quat.y, (float)p_quat.z }, (float)p_quat.w };
}

_FORCE_INLINE_ b3Quat to_b3(const Basis &p_basis) {
	return to_b3(p_basis.get_rotation_quaternion().normalized());
}

_FORCE_INLINE_ Basis to_godot(const b3Quat &p_quat) {
	return Basis(Quaternion(p_quat.v.x, p_quat.v.y, p_quat.v.z, p_quat.s));
}

_FORCE_INLINE_ Transform3D to_godot(const b3Transform &p_xform) {
	return Transform3D(to_godot(p_xform.q), to_godot(p_xform.p));
}

_FORCE_INLINE_ b3Transform to_b3(const Transform3D &p_xform) {
	return b3Transform{ to_b3(p_xform.origin), to_b3(p_xform.basis) };
}

_FORCE_INLINE_ b3WorldTransform to_b3_world(const Transform3D &p_xform) {
	b3WorldTransform xform;
	xform.p = to_b3_pos(p_xform.origin);
	xform.q = to_b3(p_xform.basis);
	return xform;
}

_FORCE_INLINE_ Transform3D world_to_godot(const b3WorldTransform &p_xform) {
	return Transform3D(to_godot(p_xform.q), pos_to_godot(p_xform.p));
}

_FORCE_INLINE_ Basis to_godot(const b3Matrix3 &p_mat) {
	return Basis(to_godot(p_mat.cx), to_godot(p_mat.cy), to_godot(p_mat.cz));
}

_FORCE_INLINE_ b3Matrix3 to_b3_matrix(const Basis &p_basis) {
	return b3Matrix3{ to_b3(p_basis.get_column(0)), to_b3(p_basis.get_column(1)), to_b3(p_basis.get_column(2)) };
}

_FORCE_INLINE_ AABB to_godot(const b3AABB &p_aabb) {
	return AABB(to_godot(p_aabb.lowerBound), to_godot(p_aabb.upperBound - p_aabb.lowerBound));
}

_FORCE_INLINE_ b3AABB to_b3(const AABB &p_aabb) {
	return b3AABB{ to_b3(p_aabb.position), to_b3(p_aabb.position + p_aabb.size) };
}

// Collision filtering. Godot's layer/mask rules are one-directional and cannot be expressed with Box3D's symmetric
// category/mask test, so every shape passes the built-in test and the real decision is made in the custom filter
// callback. The high bit keeps the category non-zero even when a Godot layer is zero.
namespace Box3DFilter {
constexpr uint64_t ALWAYS_BIT = 1ull << 63;

_FORCE_INLINE_ uint64_t category_bits(uint32_t p_layer) {
	return ALWAYS_BIT | (uint64_t)p_layer;
}
_FORCE_INLINE_ b3Filter make(uint32_t p_layer) {
	b3Filter filter = b3DefaultFilter();
	filter.categoryBits = category_bits(p_layer);
	filter.maskBits = UINT64_MAX;
	filter.groupIndex = 0;
	return filter;
}
_FORCE_INLINE_ b3QueryFilter make_query(uint32_t p_mask) {
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = UINT64_MAX;
	filter.maskBits = (uint64_t)p_mask;
	return filter;
}
} //namespace Box3DFilter

_FORCE_INLINE_ uint64_t box3d_shape_key(const b3ShapeId &p_id) {
	return ((uint64_t)p_id.generation << 32) | (uint64_t)(uint32_t)p_id.index1;
}

namespace Box3DMath {
// Splits `p_basis` into a right-handed orthonormal basis and a per-axis scale. Returns false if the basis is singular.
bool decompose(Basis &p_basis, Vector3 &r_scale);

// Splits an affine transform into rotation, position and scale. Returns false if the transform has shear or is singular.
bool decompose_affine(const Transform3D &p_xform, Quaternion &r_rotation, Vector3 &r_scale);
} //namespace Box3DMath
