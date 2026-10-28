/**************************************************************************/
/*  panini_projection.h                                                   */
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

#include "core/math/math_funcs.h"
#include "core/math/vector2.h"
#include "core/math/vector3.h"

// The Panini projection of CameraAttributes (panini_*), shared by the renderers' lens resample (which fills its
// parameters into the CameraEffects buffer) and by Camera3D's project/unproject functions, so a position the camera
// reports is where the object lands in the resampled image. camera_lens_inc.glsl (and its Compatibility copy) has the
// inverse mapping in GLSL: keep the three in step.
//
// Model: Sharpless, Ballestrem and Thomas, "Pannini: A New Projection for Rendering Wide Angle Perspective Images",
// Computational Aesthetics 2010. The scene is projected from the eye onto a unit cylinder around the vertical axis
// (azimuth phi, height h = the tangent of the elevation), and the cylinder is then viewed in perspective from a point
// p_distance (d) behind its axis, onto the plane touching it straight ahead:
//   X = S sin(phi), Y = S h, S = (d + 1) / (d + cos(phi)).
// d = 0 is the rectilinear (pinhole) projection itself; d = 1 is the standard Panini, stereographic horizontally,
// which keeps vertical lines and lines through the center straight and is conformal along the horizon (things at the
// sides keep their proportions instead of stretching sideways).
//
// Vertical compression (p_vertical_compression, c in [0, 1]) blends the vertical coordinate from the rectilinear one
// (c = 0: Y = y, horizontal lines stay straight but things at the sides look taller than they are wide) to the
// Panini's own (c = 1: Y = S h, the paper's projection). It has no effect at d = 0, where both are y.
//
// Positions are tangent-plane coordinates at unit distance in front of the eye (x = tan(phi), y = h / cos(phi) for the
// rectilinear image), with the frame centered on the view axis. A frustum with an offset is treated as centered on
// the frame (the renderers have no other center to use), consistently in the shader and in Camera3D.
namespace PaniniProjection {

// The vertical scale at azimuth phi relative to the rectilinear image's: Y = y * vertical_factor.
inline double vertical_factor(double p_cos_phi, double p_distance, double p_vertical_compression) {
	const double s = (p_distance + 1.0) / (p_distance + p_cos_phi);
	return Math::lerp(1.0, s * p_cos_phi, p_vertical_compression);
}

// Rectilinear tangent-plane position -> Panini plane position. For a point in front of the eye.
inline Vector2 from_rectilinear(const Vector2 &p_tan, double p_distance, double p_vertical_compression) {
	const double x = p_tan.x;
	const double cos_phi = 1.0 / Math::sqrt(1.0 + x * x);
	const double sin_phi = x * cos_phi;
	const double s = (p_distance + 1.0) / (p_distance + cos_phi);
	return Vector2(s * sin_phi, p_tan.y * vertical_factor(cos_phi, p_distance, p_vertical_compression));
}

// The azimuth whose Panini X is p_x: X (d + cos(phi)) = (d + 1) sin(phi), i.e. with k = X / (d + 1),
// sin(phi) - k cos(phi) = k d, so phi = atan(k) + asin(k d / sqrt(1 + k^2)). Defined for every X when d <= 1.
inline double azimuth(double p_x, double p_distance) {
	const double k = p_x / (p_distance + 1.0);
	return Math::atan(k) + Math::asin(CLAMP(k * p_distance / Math::sqrt(1.0 + k * k), -1.0, 1.0));
}

// Panini plane position -> view direction (x right, y up, z toward the scene; not normalized). Reaches past 90
// degrees of azimuth, so it also answers for points outside the rendered frame.
inline Vector3 to_direction(const Vector2 &p_panini, double p_distance, double p_vertical_compression) {
	const double phi = azimuth(p_panini.x, p_distance);
	const double cos_phi = Math::cos(phi);
	// Y = y * factor and y = h / cos(phi), so the cylinder height is h = Y cos(phi) / factor.
	const double h = p_panini.y * cos_phi / vertical_factor(cos_phi, p_distance, p_vertical_compression);
	return Vector3(Math::sin(phi), h, cos_phi);
}

// Panini plane position -> rectilinear tangent-plane position, for a point inside the rendered frame.
inline Vector2 to_rectilinear(const Vector2 &p_panini, double p_distance, double p_vertical_compression) {
	const Vector3 dir = to_direction(p_panini, p_distance, p_vertical_compression);
	return Vector2(dir.x / dir.z, dir.y / dir.z);
}

// The scale from output frame positions to Panini plane positions, relative to the rendered frame's (< 1, a zoom in),
// that shows no border: the rectilinear frame's side edge x = t lands at X = S sin(phi_e) = S cos(phi_e) t, and its
// top edge bows toward the horizon monotonically, reaching S cos(phi_e) of its height at the corner (exactly, for
// vertical compression 1; less for less compression). So the scale S cos(phi_e) puts the output's corners on the
// image's and keeps the full horizontal field of view; the vertical one narrows by the same factor at the center.
inline double fit_scale(double p_tan_half_x, double p_distance) {
	const double cos_phi = 1.0 / Math::sqrt(1.0 + p_tan_half_x * p_tan_half_x);
	return (p_distance + 1.0) * cos_phi / (p_distance + cos_phi);
}

} // namespace PaniniProjection
