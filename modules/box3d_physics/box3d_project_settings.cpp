/**************************************************************************/
/*  box3d_project_settings.cpp                                            */
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

#include "box3d_project_settings.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"

#include <box3d/box3d.h>

void Box3DProjectSettings::register_settings() {
	const b3WorldDef defaults = b3DefaultWorldDef();

	GLOBAL_DEF(PropertyInfo(Variant::INT, "physics/box3d_physics_3d/simulation/sub_steps", PROPERTY_HINT_RANGE, U"1,16,or_greater"), 4);
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "physics/box3d_physics_3d/simulation/worker_threads", PROPERTY_HINT_RANGE, U"0,32,1"), 0);
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "physics/box3d_physics_3d/simulation/allow_sleep"), true);
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "physics/box3d_physics_3d/simulation/continuous_collision"), true);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "physics/box3d_physics_3d/simulation/contact_hertz", PROPERTY_HINT_RANGE, U"1,240,0.1,or_greater,suffix:Hz"), defaults.contactHertz);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "physics/box3d_physics_3d/simulation/contact_damping_ratio", PROPERTY_HINT_RANGE, U"0,100,0.1,or_greater"), defaults.contactDampingRatio);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "physics/box3d_physics_3d/simulation/contact_speed", PROPERTY_HINT_RANGE, U"0,100,0.01,or_greater,suffix:m/s"), defaults.contactSpeed);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "physics/box3d_physics_3d/simulation/bounce_velocity_threshold", PROPERTY_HINT_RANGE, U"0,10,0.001,or_greater,suffix:m/s"), defaults.restitutionThreshold);

	GLOBAL_DEF(PropertyInfo(Variant::INT, "physics/box3d_physics_3d/motion_queries/recovery_iterations", PROPERTY_HINT_RANGE, U"1,8,or_greater"), 4);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "physics/box3d_physics_3d/motion_queries/recovery_amount", PROPERTY_HINT_RANGE, U"0,1,0.01"), 0.4f);

	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "physics/box3d_physics_3d/collisions/cylinder_hull_sides", PROPERTY_HINT_RANGE, U"8,32,1"), 32);

	GLOBAL_DEF_RST(PropertyInfo(Variant::FLOAT, "physics/box3d_physics_3d/limits/world_boundary_shape_size", PROPERTY_HINT_RANGE, U"2,100000,0.1,or_greater,suffix:m"), 2000.0f);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "physics/box3d_physics_3d/limits/max_linear_velocity", PROPERTY_HINT_RANGE, U"0,10000,0.01,or_greater,suffix:m/s"), defaults.maximumLinearSpeed);

	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "physics/box3d_physics_3d/diagnostics/report_unsupported_features"), true);

	read_settings();

	ProjectSettings::get_singleton()->connect("settings_changed", callable_mp_static(Box3DProjectSettings::read_settings));
}

void Box3DProjectSettings::read_settings() {
	simulation_sub_steps = GLOBAL_GET("physics/box3d_physics_3d/simulation/sub_steps");
	simulation_worker_threads = GLOBAL_GET("physics/box3d_physics_3d/simulation/worker_threads");
	simulation_allow_sleep = GLOBAL_GET("physics/box3d_physics_3d/simulation/allow_sleep");
	simulation_continuous = GLOBAL_GET("physics/box3d_physics_3d/simulation/continuous_collision");
	simulation_contact_hertz = GLOBAL_GET("physics/box3d_physics_3d/simulation/contact_hertz");
	simulation_contact_damping_ratio = GLOBAL_GET("physics/box3d_physics_3d/simulation/contact_damping_ratio");
	simulation_contact_speed = GLOBAL_GET("physics/box3d_physics_3d/simulation/contact_speed");
	simulation_restitution_threshold = GLOBAL_GET("physics/box3d_physics_3d/simulation/bounce_velocity_threshold");

	motion_query_recovery_iterations = GLOBAL_GET("physics/box3d_physics_3d/motion_queries/recovery_iterations");
	motion_query_recovery_amount = GLOBAL_GET("physics/box3d_physics_3d/motion_queries/recovery_amount");

	cylinder_hull_sides = GLOBAL_GET("physics/box3d_physics_3d/collisions/cylinder_hull_sides");

	world_boundary_shape_size = GLOBAL_GET("physics/box3d_physics_3d/limits/world_boundary_shape_size");
	max_linear_velocity = GLOBAL_GET("physics/box3d_physics_3d/limits/max_linear_velocity");

	verbose_unsupported_reports = GLOBAL_GET("physics/box3d_physics_3d/diagnostics/report_unsupported_features");
}
