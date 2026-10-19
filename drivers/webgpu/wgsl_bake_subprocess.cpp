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

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/hashfuncs.h"
#include "core/variant/dictionary.h"

#include <atomic>

#ifdef UNIX_ENABLED
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
extern char **environ;
#endif

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

// =========================================================================
// Shared pieces: content keys, the on-disk cache, the debug dump, one tool run
// =========================================================================

// SHA-256 of the stage's SPIR-V, as hex. The key for both the batch result
// table and the on-disk cache. A 64-bit hash would do for the table, but the
// cache outlives the process and is shared by every project this editor
// exports, so it gets the strong one; the cost is ~0.1 s for an export's
// worth of stages.
static String _spirv_key(const uint8_t *p_spv_ptr, int p_spv_size) {
	unsigned char hash[32];
	CryptoCore::sha256(p_spv_ptr, (size_t)p_spv_size, hash);
	return String::hex_encode_buffer(hash, 32);
}

// What one stage's bake came to. `wgsl` set: converted. `error` set: the tool
// ran and Tint rejected or crashed on it (reported at WARN when a container
// asks, exactly as the per-stage path always has). Neither: never recorded.
struct PrebakedStage {
	String wgsl;
	String error;
};

// The batch's results, read by bake_wgsl_via_subprocess() while the containers
// are built. Both sides lock; the writers are the batch's worker threads and
// the readers are WorkerThreadPool threads.
static HashMap<String, PrebakedStage> session_results;
static Mutex session_mutex;

// Bumped if the meaning of a cache entry changes (what the WGSL text is
// expected to contain, independent of the tool that produced it). The tool's
// own hash is part of the path too, so this is for changes on this side only.
static constexpr const char *CACHE_FORMAT_VERSION = "1";
static constexpr uint64_t CACHE_MAX_AGE_SEC = 30ull * 24 * 60 * 60;

// Where cached WGSL lives, or empty when caching is off. Resolved once.
static const String &_cache_root() {
	static const String root = [] {
		OS *os = OS::get_singleton();
		if (os->get_environment("WEBGPU_BAKE_CACHE") == "0") {
			return String();
		}
		String dir = os->get_environment("WEBGPU_BAKE_CACHE_DIR");
		if (dir.is_empty()) {
			String cache_path = os->get_cache_path();
			if (cache_path.is_empty()) {
				return String();
			}
			dir = cache_path.path_join(os->get_godot_dir_name()).path_join("webgpu_wgsl_bake");
		}
		Error err = DirAccess::make_dir_recursive_absolute(dir);
		if (err != OK && err != ERR_ALREADY_EXISTS) {
			WARN_PRINT(vformat("WebGPU shader baker: couldn't create the WGSL cache directory '%s' (error %d); baking without a cache.", dir, (int)err));
			return String();
		}
		return dir;
	}();
	return root;
}

// Per-tool subdirectory name: the hash of the tint_convert_cli binary itself.
// Every input to the conversion other than the SPIR-V (the preprocessing
// passes, vendored Tint, their build flags) is baked into that file, so hashing
// it is the one check that catches all of them. Resolved once per process.
static const String &_tool_cache_dir_name() {
	static const String name = [] {
		String tool = _find_tint_convert_cli();
		if (tool.is_empty()) {
			return String();
		}
		String sha = FileAccess::get_sha256(tool);
		if (sha.is_empty()) {
			return String();
		}
		return vformat("v%s-%s", CACHE_FORMAT_VERSION, sha.left(24));
	}();
	return name;
}

static String _cache_entry_path(const String &p_key) {
	const String &root = _cache_root();
	const String &tool_dir = _tool_cache_dir_name();
	if (root.is_empty() || tool_dir.is_empty()) {
		return String();
	}
	return root.path_join(tool_dir).path_join(p_key.left(2)).path_join(p_key + ".wgsl");
}

static bool _cache_read(const String &p_key, String &r_wgsl) {
	String path = _cache_entry_path(p_key);
	if (path.is_empty()) {
		return false;
	}
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_null()) {
		return false;
	}
	uint64_t len = f->get_length();
	if (len == 0) {
		return false;
	}
	Vector<uint8_t> bytes;
	bytes.resize(len);
	if (f->get_buffer(bytes.ptrw(), len) != len) {
		return false;
	}
	r_wgsl.append_utf8((const char *)bytes.ptr(), len);
	return !r_wgsl.is_empty();
}

// Temp file plus rename, so a reader (another export, or this one's other
// threads) never sees a half-written entry. Losing the race to another writer
// is fine: both wrote the same bytes.
static void _cache_write(const String &p_key, const String &p_wgsl) {
	String path = _cache_entry_path(p_key);
	if (path.is_empty() || p_wgsl.is_empty()) {
		return;
	}
	Error err = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		return;
	}
	String tmp_path = vformat("%s.%d.tmp", path, OS::get_singleton()->get_process_id());
	{
		Ref<FileAccess> f = FileAccess::open(tmp_path, FileAccess::WRITE);
		if (f.is_null()) {
			return;
		}
		CharString utf8 = p_wgsl.utf8();
		f->store_buffer((const uint8_t *)utf8.get_data(), utf8.length());
	}
	if (DirAccess::rename_absolute(tmp_path, path) != OK) {
		DirAccess::remove_absolute(tmp_path);
	}
}

// Deletes entries older than CACHE_MAX_AGE_SEC and the directories that empties,
// including whole per-tool trees left by Tint builds no longer in use. Walks
// the fixed three-level layout only; nothing recursive on unknown input.
static void _cache_prune() {
	const String &root = _cache_root();
	if (root.is_empty()) {
		return;
	}
	const uint64_t now = (uint64_t)OS::get_singleton()->get_unix_time();
	uint32_t removed = 0;
	for (const String &tool_dir_name : DirAccess::get_directories_at(root)) {
		String tool_dir = root.path_join(tool_dir_name);
		for (const String &bucket_name : DirAccess::get_directories_at(tool_dir)) {
			String bucket = tool_dir.path_join(bucket_name);
			for (const String &file_name : DirAccess::get_files_at(bucket)) {
				String file = bucket.path_join(file_name);
				uint64_t mtime = FileAccess::get_modified_time(file);
				const uint64_t age = (mtime != 0 && now > mtime) ? now - mtime : 0;
				// A .tmp is another export's entry mid-write unless it is hours
				// old, in which case its writer died and left it behind.
				const bool stale_tmp = file_name.ends_with(".tmp") && age > 60 * 60;
				if (stale_tmp || age > CACHE_MAX_AGE_SEC) {
					if (DirAccess::remove_absolute(file) == OK) {
						removed++;
					}
				}
			}
			if (DirAccess::get_files_at(bucket).is_empty()) {
				DirAccess::remove_absolute(bucket);
			}
		}
		if (DirAccess::get_directories_at(tool_dir).is_empty()) {
			DirAccess::remove_absolute(tool_dir);
		}
	}
	if (removed > 0) {
		print_verbose(vformat("WebGPU shader baker: pruned %d stale entries from the WGSL cache at '%s'.", removed, root));
	}
}

// Debug aid: WEBGPU_BAKE_DEBUG_DUMP=<dir> copies every SPIR-V that fails to
// bake into that directory (instead of the usual delete-after-use) for offline
// repro with tint_convert_cli <file.spv> directly. Add
// WEBGPU_BAKE_DEBUG_DUMP_ALL=1 to dump *every* shader regardless of success --
// needed to catch a bug that doesn't fail to bake at all (produces
// valid-but-wrong WGSL, e.g. Task 23's dead-resource regression), where the
// failure only ever shows up later, at real CreatePipelineLayout/
// CreateShaderModule time.
static void _debug_dump(const String &p_temp_path, bool p_failed) {
	String dump_dir = OS::get_singleton()->get_environment("WEBGPU_BAKE_DEBUG_DUMP");
	if (dump_dir.is_empty()) {
		return;
	}
	bool dump_all = OS::get_singleton()->has_environment("WEBGPU_BAKE_DEBUG_DUMP_ALL");
	if (!dump_all && !p_failed) {
		return;
	}
	// Every outcome below says something, at WARN: an earlier session recorded
	// this hook producing an empty directory against real failing shaders and
	// had no way to tell whether the condition above never fired or the copy
	// itself failed (webgpu_notes/TASKS.md Task 20). A debug aid that can fail
	// silently is worse than none.
	String dump_path = dump_dir.path_join(p_temp_path.get_file());
	Error dir_err = DirAccess::make_dir_recursive_absolute(dump_dir);
	if (dir_err != OK && dir_err != ERR_ALREADY_EXISTS) {
		WARN_PRINT(vformat("WEBGPU_BAKE_DEBUG_DUMP: couldn't create '%s' (error %d); not dumping.", dump_dir, (int)dir_err));
		return;
	}
	Ref<FileAccess> src = FileAccess::open(p_temp_path, FileAccess::READ);
	Ref<FileAccess> dst = src.is_valid() ? FileAccess::open(dump_path, FileAccess::WRITE) : Ref<FileAccess>();
	if (src.is_null()) {
		WARN_PRINT(vformat("WEBGPU_BAKE_DEBUG_DUMP: couldn't reopen '%s' to copy it; not dumping.", p_temp_path));
	} else if (dst.is_null()) {
		WARN_PRINT(vformat("WEBGPU_BAKE_DEBUG_DUMP: couldn't write '%s'; not dumping.", dump_path));
	} else {
		dst->store_buffer(src->get_buffer(src->get_length()));
		WARN_PRINT(vformat("WEBGPU_BAKE_DEBUG_DUMP: wrote '%s' -- reproduce with: bin/tint_convert_cli '%s'", dump_path, dump_path));
	}
}

// Reports one {"error": ...} entry from the tool, the way the per-stage path
// always has: at WARN (not print_verbose) because this is also the only place
// that can identify *which* shader hits a Tint bug that would otherwise crash
// the browser at runtime just the same, and truncated because a SPIR-V
// validation failure's message embeds Tint's full disassembly of the module.
static String _error_text(const Variant &p_entry) {
	Dictionary error_dict = p_entry;
	String error_text = error_dict.get("error", "unknown error");
	error_text = error_text.replace("\n", " | ");
	if (error_text.length() > 300) {
		error_text = error_text.left(300) + "... [truncated]";
	}
	return error_text;
}

// Runs `tint_convert_cli --batch <files...>` once and returns its stdout.
// p_stdout_path is where the output is captured on Unix (a file the child
// writes directly, so there is no pipe to drain); unused on Windows.
//
// Unix uses posix_spawn(), not OS::execute(): the latter is fork()+execvp()
// with this process's own code -- CharString conversions, Vector growth --
// running in the child between the two. Call that from many threads at once
// and a child can inherit a lock another thread held at fork time, with no
// thread left to release it; an export reproducibly hung that way until every
// spawn was serialized behind one mutex (which is what made the bake serial in
// the first place). posix_spawn() runs none of this process's code in the
// child, so the batches can genuinely run side by side.
//
// Returns false if the tool could not be run at all. r_exit_code is the
// tool's exit status when it did run.
static bool _run_tool(const String &p_tool, const Vector<String> &p_args, const String &p_stdout_path, String &r_output, int &r_exit_code) {
	r_exit_code = -1;
#ifdef UNIX_ENABLED
	LocalVector<CharString> arg_storage;
	arg_storage.push_back(p_tool.utf8());
	for (const String &arg : p_args) {
		arg_storage.push_back(arg.utf8());
	}
	LocalVector<char *> argv;
	for (CharString &cs : arg_storage) {
		argv.push_back(const_cast<char *>(cs.get_data()));
	}
	argv.push_back(nullptr);

	CharString stdout_cs = p_stdout_path.utf8();
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, stdout_cs.get_data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

	pid_t pid = 0;
	int spawn_err = posix_spawn(&pid, argv[0], &actions, nullptr, argv.ptr(), environ);
	posix_spawn_file_actions_destroy(&actions);
	if (spawn_err != 0) {
		return false;
	}

	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			return false;
		}
	}
	if (!WIFEXITED(status)) {
		r_exit_code = -1;
		return true;
	}
	r_exit_code = WEXITSTATUS(status);
	r_output = FileAccess::get_file_as_string(p_stdout_path);
	return true;
#else
	// CreateProcess() has no fork step, so OS::execute() is safe to call from
	// several threads at once here.
	List<String> args;
	for (const String &arg : p_args) {
		args.push_back(arg);
	}
	Error err = OS::get_singleton()->execute(p_tool, args, &r_output, &r_exit_code, false);
	return err == OK;
#endif
}

// =========================================================================
// Per-stage entry point (what the container calls)
// =========================================================================

// " [ShaderName (Fragment stage)]", or nothing when the caller gave no context,
// so these messages read exactly as before where one is not available.
static String _context_suffix(const String &p_context) {
	return p_context.is_empty() ? String() : vformat(" [%s]", p_context);
}

String bake_wgsl_via_subprocess(const uint8_t *p_spv_ptr, int p_spv_size, bool *r_wgsl_impossible, const String &p_context) {
	if (r_wgsl_impossible != nullptr) {
		*r_wgsl_impossible = false;
	}
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
		print_verbose(vformat("WebGPU shader baker: not baking one shader stage%s -- it %s. The WebGPU renderer does not use this variant.", _context_suffix(p_context), unsupported));
		if (r_wgsl_impossible != nullptr) {
			*r_wgsl_impossible = true;
		}
		return String();
	}

	const String key = _spirv_key(p_spv_ptr, p_spv_size);

	// The batch that ran before the containers were built has normally already
	// answered this. A recorded failure is reported here, once per stage that
	// asks, which is how the per-stage path always reported it.
	{
		MutexLock lock(session_mutex);
		const PrebakedStage *prebaked = session_results.getptr(key);
		if (prebaked != nullptr) {
			if (!prebaked->wgsl.is_empty()) {
				return prebaked->wgsl;
			}
			WARN_PRINT(vformat("WebGPU shader baker: leaving one shader stage unbaked%s (tint_convert_cli: %s).", _context_suffix(p_context), prebaked->error));
			return String();
		}
	}

	if (String cached; _cache_read(key, cached)) {
		return cached;
	}

	// Not covered by the batch: convert this one stage on its own. Unique temp
	// filename: the content key plus a monotonic counter, since two distinct
	// stages could otherwise share identical SPIR-V bytes at the same instant.
	static std::atomic<uint32_t> counter{ 0 };
	String temp_path = OS::get_singleton()->get_temp_path().path_join(
			vformat("godot_webgpu_bake_%s_%d.spv", key.left(16), counter.fetch_add(1)));

	{
		Ref<FileAccess> f = FileAccess::open(temp_path, FileAccess::WRITE);
		if (f.is_null()) {
			WARN_PRINT(vformat("WebGPU shader baker: couldn't write temp file '%s'; leaving this shader unbaked%s.", temp_path, _context_suffix(p_context)));
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
		// OS_Unix::execute() runs the subprocess via fork()+exec(), with this
		// process's own code running in the child before the exec. Without this
		// lock, many threads could call fork() concurrently while other threads
		// are mid-allocation (holding libc's internal malloc/lock state). A forked
		// child only gets the calling thread; if another thread held such a lock
		// at the moment of fork(), the child inherits it permanently locked with
		// no thread left alive to release it, and the very next allocation in
		// that child hangs forever -- confirmed in practice: export against a
		// real project reproducibly stalled partway through baking (same ~74%
		// mark every time) until this lock was added. This path is now only the
		// fallback for a stage WgslBakeBatch did not cover, so the serialization
		// costs nothing in the normal case.
		static Mutex subprocess_mutex;
		MutexLock lock(subprocess_mutex);
		err = OS::get_singleton()->execute(tint_convert_cli, args, &output, &exit_code, false);
	}

	bool this_failed = err != OK || exit_code != 0;
	Variant parsed = this_failed ? Variant() : JSON::parse_string(output);
	Dictionary result = parsed;
	Variant entry = result.is_empty() ? Variant() : result[temp_path];
	_debug_dump(temp_path, this_failed || entry.get_type() != Variant::STRING);
	DirAccess::remove_absolute(temp_path);

	if (this_failed) {
		WARN_PRINT(vformat("WebGPU shader baker: tint_convert_cli itself failed to run (exit %d); leaving this shader unbaked%s.", exit_code, _context_suffix(p_context)));
		return String();
	}
	if (result.is_empty()) {
		WARN_PRINT(vformat("WebGPU shader baker: couldn't parse tint_convert_cli's JSON output; leaving this shader unbaked%s.", _context_suffix(p_context)));
		return String();
	}
	if (entry.get_type() == Variant::STRING) {
		String wgsl = entry;
		_cache_write(key, wgsl);
		return wgsl;
	}

	// {"error": "..."} -- expected on a shader variant Tint can't handle
	// (including an internal-compiler-error abort in the isolated child);
	// not an error in the export itself, just a shader left to the runtime
	// fallback.
	WARN_PRINT(vformat("WebGPU shader baker: leaving one shader stage unbaked%s (tint_convert_cli: %s).", _context_suffix(p_context), _error_text(entry)));
	return String();
}

// =========================================================================
// WgslBakeBatch
// =========================================================================

WgslBakeBatch::WgslBakeBatch() {
	MutexLock lock(session_mutex);
	session_results.clear();
}

WgslBakeBatch::~WgslBakeBatch() {
	if (started && !finished) {
		cancel();
		finish();
	}
}

void WgslBakeBatch::add_stage(const Vector<uint8_t> &p_spirv) {
	ERR_FAIL_COND(started);
	stage_count++;
	if (p_spirv.size() < 20 || (p_spirv.size() % 4) != 0) {
		return; // The per-stage path rejects it the same way.
	}
	if (!_wgsl_unsupported_reason(p_spirv.ptr(), (int)p_spirv.size()).is_empty()) {
		impossible_count++; // Screened again, and explained, when the container asks.
		return;
	}
	String key = _spirv_key(p_spirv.ptr(), (int)p_spirv.size());
	if (seen_keys.has(key)) {
		duplicate_count++;
		return;
	}
	seen_keys.insert(key);

	if (String cached; _cache_read(key, cached)) {
		cache_hit_count++;
		MutexLock lock(session_mutex);
		session_results[key].wgsl = cached;
		return;
	}

	Entry entry;
	entry.key = key;
	entry.spirv = p_spirv;
	entries.push_back(entry);
}

void WgslBakeBatch::start() {
	ERR_FAIL_COND(started);
	started = true;
	started_usec = OS::get_singleton()->get_ticks_usec();
	if (entries.is_empty()) {
		return;
	}
	tool_path = _find_tint_convert_cli();
	if (tool_path.is_empty()) {
		return; // Already warned; every stage ships unbaked, as before.
	}

	// One directory per batch: two editors exporting at once must not share
	// file names, and one rmdir at the end is the cleanup.
	static std::atomic<uint32_t> batch_counter{ 0 };
	temp_dir = OS::get_singleton()->get_temp_path().path_join(
			vformat("godot_webgpu_bake_%d_%d", OS::get_singleton()->get_process_id(), batch_counter.fetch_add(1)));
	Error err = DirAccess::make_dir_recursive_absolute(temp_dir);
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		WARN_PRINT(vformat("WebGPU shader baker: couldn't create '%s' (error %d); baking stages one at a time instead.", temp_dir, (int)err));
		temp_dir = String();
		return;
	}

	LocalVector<uint32_t> ready;
	for (uint32_t i = 0; i < entries.size(); i++) {
		Entry &entry = entries[i];
		entry.temp_path = temp_dir.path_join(entry.key + ".spv");
		Ref<FileAccess> f = FileAccess::open(entry.temp_path, FileAccess::WRITE);
		if (f.is_null()) {
			WARN_PRINT(vformat("WebGPU shader baker: couldn't write '%s'; that stage will be baked on its own.", entry.temp_path));
			entry.temp_path = String();
			continue;
		}
		f->store_buffer(entry.spirv.ptr(), entry.spirv.size());
		ready.push_back(i);
	}
	if (ready.is_empty()) {
		return;
	}

	// Largest first, dealt round-robin, so no chunk ends up holding all the
	// scene-shader giants while the others finish early.
	struct SizedIndex {
		int64_t size = 0;
		uint32_t index = 0;
		bool operator<(const SizedIndex &p_other) const { return size > p_other.size; }
	};
	LocalVector<SizedIndex> ordered;
	for (uint32_t index : ready) {
		ordered.push_back({ entries[index].spirv.size(), index });
	}
	ordered.sort();

	int jobs = OS::get_singleton()->get_processor_count();
	if (String env = OS::get_singleton()->get_environment("WEBGPU_BAKE_JOBS"); env.is_valid_int()) {
		jobs = env.to_int();
	}
	jobs = CLAMP(jobs, 1, 64);
	// Several chunks per worker so a slow chunk is absorbed by the others, but
	// never so many that spawning dominates again: ~16 files per invocation at
	// an export's usual size.
	uint32_t chunk_count = MIN((uint32_t)ordered.size(), (uint32_t)jobs * 4);
	chunks.resize(chunk_count);
	for (uint32_t i = 0; i < ordered.size(); i++) {
		chunks[i % chunk_count].push_back(ordered[i].index);
	}

	uint32_t worker_count = MIN((uint32_t)jobs, chunk_count);
	running_workers.store(worker_count);
	for (uint32_t i = 0; i < worker_count; i++) {
		Thread *thread = memnew(Thread);
		thread->start(_worker_thread, this);
		threads.push_back(thread);
	}
}

bool WgslBakeBatch::is_done() const {
	return !started || running_workers.load() == 0;
}

void WgslBakeBatch::_worker_thread(void *p_self) {
	WgslBakeBatch *self = (WgslBakeBatch *)p_self;
	while (!self->cancelled.load()) {
		uint32_t index = self->next_chunk.fetch_add(1);
		if (index >= self->chunks.size()) {
			break;
		}
		self->_run_chunk(index);
	}
	self->running_workers.fetch_sub(1);
}

void WgslBakeBatch::_run_chunk(uint32_t p_chunk_index) {
	const LocalVector<uint32_t> &chunk = chunks[p_chunk_index];
	Vector<String> args;
	args.push_back("--batch");
	for (uint32_t entry_index : chunk) {
		args.push_back(entries[entry_index].temp_path);
	}

	String output;
	int exit_code = -1;
	bool ran = _run_tool(tool_path, args, temp_dir.path_join(vformat("chunk_%d.json", p_chunk_index)), output, exit_code);
	Variant parsed = (ran && exit_code == 0) ? JSON::parse_string(output) : Variant();
	Dictionary result = parsed;
	if (!ran || exit_code != 0 || result.is_empty()) {
		// The tool itself, not Tint: leave these stages unrecorded so the
		// per-stage path retries each on its own and reports what it sees.
		tool_failure_count.fetch_add(1);
		WARN_PRINT(vformat("WebGPU shader baker: a tint_convert_cli batch of %d stages %s; those stages will be retried one at a time.", chunk.size(), ran ? vformat("exited with code %d", exit_code) : String("could not be started")));
		done.fetch_add(chunk.size());
		return;
	}

	for (uint32_t entry_index : chunk) {
		const Entry &entry = entries[entry_index];
		Variant value = result.get(entry.temp_path, Variant());
		PrebakedStage stage;
		if (value.get_type() == Variant::STRING) {
			stage.wgsl = value;
			baked_count.fetch_add(1);
			_cache_write(entry.key, stage.wgsl);
		} else {
			stage.error = value.get_type() == Variant::DICTIONARY ? _error_text(value) : String("no result for this file in the tool's output");
			failed_count.fetch_add(1);
		}
		_debug_dump(entry.temp_path, stage.wgsl.is_empty());
		MutexLock lock(session_mutex);
		session_results[entry.key] = stage;
	}
	done.fetch_add(chunk.size());
}

void WgslBakeBatch::finish() {
	if (finished) {
		return;
	}
	finished = true;
	for (Thread *thread : threads) {
		thread->wait_to_finish();
		memdelete(thread);
	}
	threads.clear();

	if (!temp_dir.is_empty()) {
		for (const String &file : DirAccess::get_files_at(temp_dir)) {
			DirAccess::remove_absolute(temp_dir.path_join(file));
		}
		DirAccess::remove_absolute(temp_dir);
	}

	_cache_prune();

	const double seconds = double(OS::get_singleton()->get_ticks_usec() - started_usec) / 1000000.0;
	print_verbose(vformat("WebGPU shader baker: %d stages -> %d unique to convert (%d duplicates, %d unconvertible, %d from the WGSL cache); %d converted, %d rejected by Tint, %d tool failures, %d batches in %.2f s%s.",
			stage_count, (int)entries.size(), duplicate_count, impossible_count, cache_hit_count,
			baked_count.load(), failed_count.load(), tool_failure_count.load(), (int)chunks.size(), seconds,
			cancelled.load() ? String(" (cancelled)") : String()));
}

} // namespace webgpu
