/**************************************************************************/
/*  wgsl_bake_subprocess.cpp                                              */
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

#include "wgsl_bake_subprocess.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/templates/hashfuncs.h"
#include "core/variant/dictionary.h"

#include <atomic>

namespace webgpu {

// Reports why a SPIR-V module can never become WGSL, or an empty string when
// nothing rules it out up front.
//
// This is not a guess at what Tint happens to reject today -- all three cases
// below are WGSL language limitations with no workaround, and Tint's SPIR-V
// reader aborts the process on them (TINT_ASSERT / TINT_UNIMPLEMENTED) rather
// than returning an error. Checking first means not forking a child just to
// watch it die, and not reporting a structural impossibility as if it were a
// surprise.
//
// The variants that hit this are ones the WebGPU renderer never selects: the
// shader baker runs inside the editor and enumerates every variant the
// *editor's* RenderingDevice declares, so a Vulkan editor offers FFX_HALF and
// image-atomic variants that RenderingDeviceDriverWebGPU's own capability
// checks never ask for (see RendererRD::FSR2Effect's modes_with_fp16 and
// modes_atomic_fallback), and it compiles USE_MULTIVIEW variants even with XR
// off (RendererRD::VRS leaves VRS_RG_MULTIVIEW enabled). They would be unusable
// on WebGPU regardless, since what stops them is WGSL itself.
//
// The ViewIndex rule is deliberately specific to that one builtin, and must NOT
// be generalized to "any BuiltIn missing from Tint's reader". This check runs on
// *raw* SPIR-V, before spirv_preprocess, and some builtins Tint does not list
// are rewritten away by a pass before Tint ever sees them -- HelperInvocation is
// absent from Tint's list yet converts fine, because strip_helper_invocation_builtin()
// removes it (cluster_render.glsl relies on this). A general rule would skip
// shaders that bake perfectly well.
static String _wgsl_unsupported_reason(const uint8_t *p_spv_ptr, int p_spv_size) {
	static constexpr uint16_t OP_TYPE_INT = 21;
	static constexpr uint16_t OP_IMAGE_TEXEL_POINTER = 60;
	static constexpr uint16_t OP_DECORATE = 71;
	static constexpr uint16_t OP_MEMBER_DECORATE = 72;
	static constexpr uint32_t DECORATION_BUILT_IN = 11;
	static constexpr uint32_t BUILT_IN_VIEW_INDEX = 4440;

	const uint32_t total_words = (uint32_t)(p_spv_size / 4);
	if (total_words < 5) {
		return String();
	}

	uint32_t pos = 5; // Skip the 5-word header.
	while (pos < total_words) {
		uint32_t word0 = 0;
		memcpy(&word0, p_spv_ptr + (size_t)pos * 4, 4);
		uint32_t word_count = word0 >> 16;
		uint16_t opcode = (uint16_t)(word0 & 0xFFFF);
		if (word_count == 0 || pos + word_count > total_words) {
			break; // Malformed; let Tint be the one to complain about it.
		}

		if (opcode == OP_TYPE_INT && word_count >= 3) {
			uint32_t width = 0;
			memcpy(&width, p_spv_ptr + ((size_t)pos + 2) * 4, 4);
			if (width != 32) {
				// WGSL has i32/u32 and nothing narrower. Tint handles a 16-bit
				// *float* (f16) but asserts on a 16-bit integer.
				return vformat("uses %d-bit integers, and WGSL has no integer type other than 32-bit", width);
			}
		} else if (opcode == OP_IMAGE_TEXEL_POINTER) {
			// Atomics on a texel, which WGSL has no equivalent for at all.
			return String("uses image atomics (OpImageTexelPointer), which WGSL does not have");
		} else if (opcode == OP_DECORATE && word_count >= 4) {
			uint32_t decoration = 0;
			memcpy(&decoration, p_spv_ptr + ((size_t)pos + 2) * 4, 4);
			uint32_t value = 0;
			memcpy(&value, p_spv_ptr + ((size_t)pos + 3) * 4, 4);
			if (decoration == DECORATION_BUILT_IN && value == BUILT_IN_VIEW_INDEX) {
				return String("uses gl_ViewIndex (multiview), which WGSL does not have");
			}
		} else if (opcode == OP_MEMBER_DECORATE && word_count >= 5) {
			uint32_t decoration = 0;
			memcpy(&decoration, p_spv_ptr + ((size_t)pos + 3) * 4, 4);
			uint32_t value = 0;
			memcpy(&value, p_spv_ptr + ((size_t)pos + 4) * 4, 4);
			if (decoration == DECORATION_BUILT_IN && value == BUILT_IN_VIEW_INDEX) {
				return String("uses gl_ViewIndex (multiview), which WGSL does not have");
			}
		}

		pos += word_count;
	}

	return String();
}

// Resolved once per process (tint_convert_cli's location can't change while
// the editor is running); empty string means "confirmed missing", so a
// missing tool only warns once instead of once per shader.
//
// Shader baking runs across many WorkerThreadPool threads concurrently (see
// ShaderBakerExportPlugin::_customize_shader_version()), so this can't be a
// plain "static bool resolved" + "static String path" pair -- multiple
// threads racing through that read-check-write on first call is a data race
// on Godot's non-atomic, COW-refcounted String (confirmed in practice: an
// export against a real project with hundreds of shaders reliably hung
// partway through baking until this was fixed). A static local initialized
// via an immediately-invoked lambda is guarded by the compiler's thread-safe
// "magic statics" guarantee (the initializer runs exactly once, and any
// thread that arrives during that initialization blocks until it
// completes), and the result is never written again afterward, so every
// later call is just a safe concurrent read.
static String _find_tint_convert_cli() {
	static const String path = [] {
		String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
#ifdef WINDOWS_ENABLED
		String candidate = exe_dir.path_join("tint_convert_cli.exe");
#else
		String candidate = exe_dir.path_join("tint_convert_cli");
#endif
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
		WARN_PRINT(vformat(
				"WebGPU shader baker: '%s' not found next to the editor executable. "
				"Shaders will be exported unbaked (still functional -- the runtime "
				"Tint fallback in the browser handles them, just without the export-time "
				"speedup). Build it with drivers/webgpu/tint_cli/build.sh and place it "
				"in the same directory as the editor executable to enable baking.",
				candidate));
		return String();
	}();
	return path;
}

String bake_wgsl_via_subprocess(const uint8_t *p_spv_ptr, int p_spv_size) {
	if (p_spv_size < 20 || (p_spv_size % 4) != 0) {
		return String();
	}

	String tint_convert_cli = _find_tint_convert_cli();
	if (tint_convert_cli.is_empty()) {
		return String();
	}

	// Checked before spawning anything: Tint aborts on these rather than
	// returning an error, so the alternative is forking a child per shader
	// purely to have it crash, and then reporting a WGSL limitation at WARN as
	// though something had gone wrong.
	if (String unsupported = _wgsl_unsupported_reason(p_spv_ptr, p_spv_size); !unsupported.is_empty()) {
		print_verbose(vformat("WebGPU shader baker: not baking one shader stage -- it %s. The WebGPU renderer does not use this variant.", unsupported));
		return String();
	}

	// Unique temp filename: content hash (two 32-bit passes, matching the
	// runtime driver's own SPIR-V cache key -- see rendering_device_driver_webgpu.cpp)
	// plus a monotonic counter, since ShaderBakerExportPlugin bakes many
	// shaders concurrently across WorkerThreadPool threads and two distinct
	// stages could otherwise share identical SPIR-V bytes at the same instant.
	static std::atomic<uint32_t> counter{ 0 };
	uint32_t hash_lo = hash_murmur3_buffer(p_spv_ptr, p_spv_size);
	uint32_t hash_hi = hash_murmur3_buffer(p_spv_ptr, p_spv_size, 0x9E3779B9);
	String temp_path = OS::get_singleton()->get_temp_path().path_join(
			vformat("godot_webgpu_bake_%08x%08x_%d.spv", hash_hi, hash_lo, counter.fetch_add(1)));

	{
		Ref<FileAccess> f = FileAccess::open(temp_path, FileAccess::WRITE);
		if (f.is_null()) {
			WARN_PRINT(vformat("WebGPU shader baker: couldn't write temp file '%s'; leaving this shader unbaked.", temp_path));
			return String();
		}
		f->store_buffer(p_spv_ptr, p_spv_size);
	}

	// --batch (not single-file mode), even for this one file: single-file
	// mode calls Tint directly with no isolation of its OWN process, so a
	// Tint internal-compiler-error abort() can dump partial/garbled
	// diagnostic bytes straight to stdout/stderr -- which OS::execute()
	// then tries to UTF-8-decode, flooding the log with "Unicode parsing
	// error" spam (confirmed in practice). --batch mode already forks each
	// file's conversion into its own child with stdout/stderr redirected to
	// /dev/null (see tint_cli/main.cpp's convert_isolated()), so a crash
	// only ever surfaces here as a clean, well-formed JSON error string.
	List<String> args;
	args.push_back("--batch");
	args.push_back(temp_path);
	String output;
	int exit_code = -1;
	Error err;
	{
		// Serialize every OS::execute() call across all baking threads.
		// OS_Unix::execute() runs the subprocess via popen(), i.e. fork()+exec().
		// ShaderBakerExportPlugin bakes many shaders in parallel across
		// WorkerThreadPool threads, so without this lock, many threads could
		// call fork() concurrently while other threads are mid-allocation
		// (holding libc's internal malloc/lock state). A forked child only
		// gets the calling thread; if another thread held such a lock at the
		// moment of fork(), the child inherits it permanently locked with no
		// thread left alive to release it, and the very next allocation in
		// that child (trivially reachable before execve() even runs) hangs
		// forever -- confirmed in practice: export against a real project
		// reproducibly stalled partway through baking (same ~74% mark every
		// time) until this lock was added. Costs the parallelism baking would
		// otherwise get from spawning subprocesses concurrently, but a
		// slower, reliable export beats a fast one that hangs.
		static Mutex subprocess_mutex;
		MutexLock lock(subprocess_mutex);
		err = OS::get_singleton()->execute(tint_convert_cli, args, &output, &exit_code, false);
	}

	// Debug aid: WEBGPU_BAKE_DEBUG_DUMP=<dir> copies every SPIR-V that fails
	// to bake into that directory (instead of the usual delete-after-use)
	// for offline repro with tint_convert_cli <file.spv> directly. Add
	// WEBGPU_BAKE_DEBUG_DUMP_ALL=1 to dump *every* shader regardless of
	// success -- needed to catch a bug that doesn't fail to bake at all
	// (produces valid-but-wrong WGSL, e.g. Task 23's dead-resource
	// regression), where the failure only ever shows up later, at real
	// CreatePipelineLayout/CreateShaderModule time.
	if (String dump_dir = OS::get_singleton()->get_environment("WEBGPU_BAKE_DEBUG_DUMP"); !dump_dir.is_empty()) {
		bool this_failed = err != OK || exit_code != 0;
		bool dump_all = OS::get_singleton()->has_environment("WEBGPU_BAKE_DEBUG_DUMP_ALL");
		Variant parsed_check = this_failed ? Variant() : JSON::parse_string(output);
		Dictionary check_dict = parsed_check;
		if (dump_all || this_failed || (!check_dict.is_empty() && Variant(check_dict[temp_path]).get_type() != Variant::STRING)) {
			// Every outcome below says something, at WARN: an earlier session
			// recorded this hook producing an empty directory against real
			// failing shaders and had no way to tell whether the condition
			// above never fired or the copy itself failed (webgpu_notes/TASKS.md
			// Task 20). A debug aid that can fail silently is worse than none.
			String dump_path = dump_dir.path_join(temp_path.get_file());
			Error dir_err = DirAccess::make_dir_recursive_absolute(dump_dir);
			if (dir_err != OK && dir_err != ERR_ALREADY_EXISTS) {
				WARN_PRINT(vformat("WEBGPU_BAKE_DEBUG_DUMP: couldn't create '%s' (error %d); not dumping.", dump_dir, (int)dir_err));
			} else {
				Ref<FileAccess> src = FileAccess::open(temp_path, FileAccess::READ);
				Ref<FileAccess> dst = src.is_valid() ? FileAccess::open(dump_path, FileAccess::WRITE) : Ref<FileAccess>();
				if (src.is_null()) {
					WARN_PRINT(vformat("WEBGPU_BAKE_DEBUG_DUMP: couldn't reopen '%s' to copy it; not dumping.", temp_path));
				} else if (dst.is_null()) {
					WARN_PRINT(vformat("WEBGPU_BAKE_DEBUG_DUMP: couldn't write '%s'; not dumping.", dump_path));
				} else {
					dst->store_buffer(src->get_buffer(src->get_length()));
					WARN_PRINT(vformat("WEBGPU_BAKE_DEBUG_DUMP: wrote '%s' -- reproduce with: bin/tint_convert_cli '%s'", dump_path, dump_path));
				}
			}
		}
	}

	DirAccess::remove_absolute(temp_path);

	if (err != OK || exit_code != 0) {
		WARN_PRINT(vformat("WebGPU shader baker: tint_convert_cli itself failed to run (exit %d); leaving this shader unbaked.", exit_code));
		return String();
	}

	Variant parsed = JSON::parse_string(output);
	Dictionary result = parsed;
	if (result.is_empty()) {
		WARN_PRINT("WebGPU shader baker: couldn't parse tint_convert_cli's JSON output; leaving this shader unbaked.");
		return String();
	}
	Variant entry = result[temp_path];
	if (entry.get_type() == Variant::STRING) {
		return entry;
	}

	// {"error": "..."} -- expected on a shader variant Tint can't handle
	// (including an internal-compiler-error abort in the isolated child);
	// not an error in the export itself, just a shader left to the runtime
	// fallback. Surfaced at WARN (not print_verbose) because this is also
	// the only place that can identify *which* shader hits a Tint bug that
	// would otherwise crash the browser at runtime just the same.
	//
	// Truncated: a SPIR-V validation failure's message embeds Tint's full
	// disassembly of the module (easily thousands of lines) -- printing that
	// in full for every such shader (confirmed in practice: 169 of them in a
	// single real-project export) balloons the log by hundreds of thousands
	// of lines and makes an otherwise-normal export look hung.
	Dictionary error_dict = entry;
	String error_text = error_dict.get("error", "unknown error");
	error_text = error_text.replace("\n", " | ");
	if (error_text.length() > 300) {
		error_text = error_text.left(300) + "... [truncated]";
	}
	WARN_PRINT(vformat("WebGPU shader baker: leaving one shader stage unbaked (tint_convert_cli: %s).", error_text));
	return String();
}

} // namespace webgpu
