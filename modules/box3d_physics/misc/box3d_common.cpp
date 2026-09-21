/**************************************************************************/
/*  box3d_common.cpp                                                      */
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

#include "box3d_common.h"

bool Box3DMath::decompose(Basis &p_basis, Vector3 &r_scale) {
	Vector3 x = p_basis.get_column(0);
	Vector3 y = p_basis.get_column(1);
	Vector3 z = p_basis.get_column(2);

	r_scale = Vector3(x.length(), y.length(), z.length());
	if (r_scale.x < CMP_EPSILON || r_scale.y < CMP_EPSILON || r_scale.z < CMP_EPSILON) {
		r_scale = Vector3(1, 1, 1);
		p_basis = Basis();
		return false;
	}

	x /= r_scale.x;
	y /= r_scale.y;
	z /= r_scale.z;

	if (x.cross(y).dot(z) < 0) {
		r_scale.z = -r_scale.z;
		z = -z;
	}

	// Gram-Schmidt to stay orthonormal in the presence of drift.
	x.normalize();
	y = (y - x * x.dot(y)).normalized();
	z = x.cross(y);

	p_basis = Basis(x, y, z);
	return true;
}

bool Box3DMath::decompose_affine(const Transform3D &p_xform, Quaternion &r_rotation, Vector3 &r_scale) {
	Basis basis = p_xform.basis;
	const Vector3 c0 = basis.get_column(0);
	const Vector3 c1 = basis.get_column(1);
	const Vector3 c2 = basis.get_column(2);

	const real_t l0 = c0.length();
	const real_t l1 = c1.length();
	const real_t l2 = c2.length();
	if (l0 < CMP_EPSILON || l1 < CMP_EPSILON || l2 < CMP_EPSILON) {
		return false;
	}

	const Vector3 n0 = c0 / l0;
	const Vector3 n1 = c1 / l1;
	const Vector3 n2 = c2 / l2;

	constexpr real_t tolerance = 1e-3;
	if (Math::abs(n0.dot(n1)) > tolerance || Math::abs(n0.dot(n2)) > tolerance || Math::abs(n1.dot(n2)) > tolerance) {
		return false;
	}

	r_scale = Vector3(l0, l1, l2);
	Vector3 z = n2;
	if (n0.cross(n1).dot(n2) < 0) {
		r_scale.z = -r_scale.z;
		z = -n2;
	}

	Basis rotation(n0, n1, z);
	rotation.orthonormalize();
	r_rotation = rotation.get_rotation_quaternion();
	return true;
}
