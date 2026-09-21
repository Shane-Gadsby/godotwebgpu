/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "box3d_physics_server_3d.h"
#include "box3d_project_settings.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/memory.h"
#include "servers/physics_3d/physics_server_3d_wrap_mt.h"

#include <box3d/box3d.h>

namespace {

void *box3d_alloc(size_t p_size, int32_t p_alignment) {
	return Memory::alloc_aligned_static(p_size, (size_t)p_alignment);
}

void box3d_free(void *p_mem, size_t p_size) {
	if (unlikely(p_mem == nullptr)) {
		return;
	}
	Memory::free_aligned_static(p_mem);
}

int box3d_assert(const char *p_condition, const char *p_file, int p_line) {
	ERR_PRINT(vformat("Box3D assertion '%s' failed at '%s:%d'.", p_condition, p_file, p_line));
	return 0;
}

void box3d_log(const char *p_message) {
	WARN_PRINT(vformat("Box3D: %s", p_message));
}

PhysicsServer3D *create_box3d_physics_server() {
#ifdef THREADS_ENABLED
	bool run_on_separate_thread = GLOBAL_GET("physics/3d/run_on_separate_thread");
#else
	bool run_on_separate_thread = false;
#endif

	Box3DPhysicsServer3D *physics_server = memnew(Box3DPhysicsServer3D(run_on_separate_thread));

	return memnew(PhysicsServer3DWrapMT(physics_server, run_on_separate_thread));
}

} // namespace

void initialize_box3d_physics_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}

	b3SetAllocator(&box3d_alloc, &box3d_free);
	b3SetAssertFcn(&box3d_assert);
	b3SetLogFcn(&box3d_log);

	PhysicsServer3DManager *manager = PhysicsServer3DManager::get_singleton();
	manager->register_server("Box3D Physics", callable_mp_static(&create_box3d_physics_server));
	// Box3D is the engine used whenever the project settings do not ask for a specific one.
	manager->set_default_server("Box3D Physics", 1);

	Box3DProjectSettings::register_settings();
}

void uninitialize_box3d_physics_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}
}
