/**************************************************************************/
/*  metahuman_riglogic.h                                                  */
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

#include "core/typedefs.h"

// The only place the module includes OpenRigLogic, so its names stay out of the module's headers. Its
// public headers are third-party code: hold them to their own warnings, not Godot's dev_mode set.
GODOT_GCC_WARNING_PUSH
GODOT_GCC_WARNING_IGNORE("-Wvirtual-inheritance")
GODOT_GCC_WARNING_IGNORE("-Wctor-dtor-privacy")
GODOT_GCC_WARNING_IGNORE("-Wnon-virtual-dtor")
GODOT_GCC_WARNING_IGNORE("-Wshadow")
GODOT_CLANG_WARNING_PUSH
GODOT_CLANG_WARNING_IGNORE("-Wnon-virtual-dtor")
GODOT_CLANG_WARNING_IGNORE("-Wshadow")
#include <riglogic/RigLogic.h>
GODOT_CLANG_WARNING_POP
GODOT_GCC_WARNING_POP

namespace MetaHumanRigLogic {

// With RotationType::Quaternions, every joint has tx ty tz qx qy qz qw sx sy sz, in both
// RigLogic::getNeutralJointValues() and RigInstance::getJointOutputs().
constexpr int JOINT_ATTRIBUTE_COUNT = 10;

} // namespace MetaHumanRigLogic
