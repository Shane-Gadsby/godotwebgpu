/**************************************************************************/
/*  box3d_diagnostics.h                                                   */
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

#include "core/string/ustring.h"

// Reports features that a script or scene tried to use but that Box3D cannot provide, with enough context to work out
// why and what would be needed to support them.
class Box3DDiagnostics {
public:
	// Prints a warning once for every distinct `p_dedupe_key` (or `p_feature` if the key is empty).
	static void report_unsupported(const String &p_feature, const String &p_attempted, const String &p_reason, const String &p_details = String(), const String &p_dedupe_key = String());
};

#define BOX3D_UNSUPPORTED(m_feature, m_attempted, m_reason, m_details) \
	Box3DDiagnostics::report_unsupported(m_feature, m_attempted, m_reason, m_details)

#define BOX3D_UNSUPPORTED_KEYED(m_feature, m_attempted, m_reason, m_details, m_key) \
	Box3DDiagnostics::report_unsupported(m_feature, m_attempted, m_reason, m_details, m_key)
