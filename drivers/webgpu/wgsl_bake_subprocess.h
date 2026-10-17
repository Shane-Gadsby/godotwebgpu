/**************************************************************************/
/*  wgsl_bake_subprocess.h                                                */
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

// Native-editor-only (WEBGPU_SHADER_BAKER_ENABLED): bakes SPIR-V -> WGSL for
// export-time shader baking by shelling out to bin/tint_convert_cli (in
// batches of stages, see WgslBakeBatch below) instead of calling Tint
// in-process.
//
// This is a deliberate, load-bearing design choice, not a simplification:
// Tint can hit an internal-compiler-error assert() on a shader variant it
// doesn't handle and abort() the whole process. Export-time baking compiles
// every declared shader variant (not just ones a game actually exercises at
// runtime), so it hits this far more often than the runtime Tint fallback
// ever does. tint_convert_cli running as a separate OS process means that
// abort() only kills the subprocess -- the editor sees a non-zero/abnormal
// exit and simply leaves that one stage unbaked (get_wgsl_code() returns
// nullptr for it, and the runtime driver's existing Tint fallback still
// handles it in the browser, exactly as if it had never been baked).
//
// Never linking Tint into the editor at all also means this file (and the
// container class that calls it) never need spirv_preprocess.cpp,
// tint_wrapper.cpp, or the vendored Tint/SPIRV-Tools sources compiled for
// linuxbsd/macos/windows -- see drivers/webgpu/SCsub.

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"

#include <atomic>

class Thread;

namespace webgpu {

// Runs bin/tint_convert_cli (located next to the running editor executable)
// on p_spv_bytes and returns the resulting WGSL, or an empty string if the
// tool is missing, the subprocess exits abnormally (including a Tint
// internal-compiler-error abort), or conversion otherwise fails. Safe to
// call concurrently from multiple WorkerThreadPool threads (shader baking
// runs in parallel; see ShaderBakerExportPlugin::_customize_shader_version()).
//
// Consults, in order: the results of the WgslBakeBatch that ran before the
// containers were built (the normal export path, see below), then the
// on-disk WGSL cache, and only then spawns a subprocess for this one stage.
// That last path is serialized across threads (see the .cpp) and exists as
// the fallback for a stage the batch did not cover.
//
// r_wgsl_impossible, when given, distinguishes the two reasons the result can
// be empty, which the caller has to tell apart: `false` means the conversion
// was attempted and did not work (so the SPIR-V is still worth shipping -- the
// runtime can try Tint itself), while `true` means the stage uses a construct
// WGSL does not have at all, so nothing could ever convert it and the bytes are
// dead weight. See _wgsl_unsupported_reason() for the exact set, and
// webgpu_notes/TASKS.md §2.27 for what shipping them was costing.
// `p_context` names the shader and stage this SPIR-V came from, purely so a
// failure can say which shader it was. Without it the baker can only report
// "one shader stage" over and over, which is what made a run of 28 failures
// impossible to act on.
String bake_wgsl_via_subprocess(const uint8_t *p_spv_ptr, int p_spv_size, bool *r_wgsl_impossible = nullptr, const String &p_context = String());

// Converts many SPIR-V stages at once, ahead of the per-container calls above.
//
// Why this exists: an export of a real project bakes ~1,500 stages, and the
// per-stage path spawns one tint_convert_cli per stage under a process-wide
// lock, so the whole bake was a serial chain of 17 ms forks on an otherwise
// idle 24-core machine -- 24 of an export's 27 seconds (webgpu_notes/TASKS.md
// §4.23). Here the stages are deduplicated by content, screened against the
// on-disk cache, written to one temp directory and handed to a handful of
// `tint_convert_cli --batch` invocations that run concurrently, each over a
// chunk of files. The tool's isolation of Tint (one forked child per module,
// inside a single-threaded process) is untouched, so an abort still only
// loses that one stage. Spawning uses posix_spawn() on Unix, which does not
// run any of this process's code in the child, so no fork-safety lock is
// needed around it.
//
// Lifecycle: add_stage() for every stage, start(), poll is_done() (the editor
// drives its progress bar from get_done_count()), finish(). Results are
// published to a process-wide table that bake_wgsl_via_subprocess() reads, so
// the container code needs no change. The table is cleared when a new batch
// is constructed; exports are modal, so there is never more than one.
//
// The on-disk cache (also read by the per-stage path) lives under the
// editor's cache directory, keyed by the SHA-256 of the stage's SPIR-V under a
// directory named for the SHA-256 of the tint_convert_cli binary -- so a
// rebuilt Tint invalidates everything, and nothing else does. The WGSL for a
// stage is a pure function of those two inputs, which is what makes caching
// across engine commits sound where the engine's own shader cache (keyed on
// GODOT_VERSION_HASH) is not. Only successful conversions are stored: a cached
// failure would turn a one-off crash into a permanently unbaked shader with no
// visible symptom. Entries older than 30 days are pruned at finish().
// WEBGPU_BAKE_CACHE=0 disables it, WEBGPU_BAKE_CACHE_DIR relocates it, and
// WEBGPU_BAKE_JOBS caps the concurrency.
class WgslBakeBatch {
	struct Entry {
		String key;
		Vector<uint8_t> spirv;
		String temp_path;
	};

	LocalVector<Entry> entries; // Unique stages that need the tool.
	HashSet<String> seen_keys;
	LocalVector<LocalVector<uint32_t>> chunks;
	LocalVector<Thread *> threads;
	String temp_dir;
	String tool_path;
	uint64_t started_usec = 0;
	bool started = false;
	bool finished = false;

	uint32_t stage_count = 0;
	uint32_t duplicate_count = 0;
	uint32_t impossible_count = 0;
	uint32_t cache_hit_count = 0;

	std::atomic<uint32_t> next_chunk{ 0 };
	std::atomic<uint32_t> done{ 0 };
	std::atomic<uint32_t> running_workers{ 0 };
	std::atomic<uint32_t> baked_count{ 0 };
	std::atomic<uint32_t> failed_count{ 0 };
	std::atomic<uint32_t> tool_failure_count{ 0 };
	std::atomic<bool> cancelled{ false };

	static void _worker_thread(void *p_self);
	void _run_chunk(uint32_t p_chunk_index);

public:
	void add_stage(const Vector<uint8_t> &p_spirv);
	void start();
	bool is_done() const;
	uint32_t get_done_count() const { return done.load(); }
	// Stages the tool actually has to convert (after deduplication and cache
	// hits); what the progress bar should count to.
	uint32_t get_total_count() const { return (uint32_t)entries.size(); }
	void cancel() { cancelled.store(true); }
	void finish();

	WgslBakeBatch();
	~WgslBakeBatch();
};

} // namespace webgpu
