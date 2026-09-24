/**************************************************************************/
/*  main.cpp                                                              */
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

// tint_convert_cli — Standalone SPIR-V → WGSL converter for build-time precompilation.
//
// Runs the same 23 preprocessing passes as the Godot WebGPU runtime driver,
// then converts to WGSL via Tint. Produces output identical to what the engine
// generates at runtime, enabling precompilation of ubershader and specialized
// shader variants at build time.
//
// Usage:
//   tint_convert_cli <file.spv>                       # single file → WGSL to stdout
//   tint_convert_cli --batch <file1.spv> <file2.spv>  # batch → JSON to stdout

#include "../spirv_preprocess.h"
#include "../tint_wrapper.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
static std::wstring utf8_to_wide(const char *p_str) {
	int len = MultiByteToWideChar(CP_UTF8, 0, p_str, -1, nullptr, 0);
	if (len <= 0) {
		return {};
	}
	std::wstring out((size_t)len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, p_str, -1, out.data(), len);
	out.resize((size_t)len - 1);
	return out;
}

static std::string wide_to_utf8(const wchar_t *p_str) {
	int len = WideCharToMultiByte(CP_UTF8, 0, p_str, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) {
		return {};
	}
	std::string out((size_t)len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, p_str, -1, out.data(), len, nullptr, nullptr);
	out.resize((size_t)len - 1);
	return out;
}
#endif

// Read a binary file into a byte vector. p_path is UTF-8 on every platform.
static std::vector<uint8_t> read_file(const char *p_path) {
#ifdef _WIN32
	// The narrow std::ifstream constructor interprets the path in the ANSI
	// code page, which can't represent every UTF-8 path (e.g. a temp dir
	// under a non-ASCII user name).
	std::ifstream f(utf8_to_wide(p_path), std::ios::binary | std::ios::ate);
#else
	std::ifstream f(p_path, std::ios::binary | std::ios::ate);
#endif
	if (!f.is_open()) {
		return {};
	}
	auto size = f.tellg();
	if (size <= 0) {
		return {};
	}
	std::vector<uint8_t> buf((size_t)size);
	f.seekg(0);
	f.read(reinterpret_cast<char *>(buf.data()), size);
	return buf;
}

// Run the full SPIR-V preprocessing pipeline + Tint conversion.
// Returns WGSL string on success, empty string on failure (error written to r_error).
static std::string convert_spirv_to_wgsl(const std::vector<uint8_t> &p_spv_bytes, std::string &r_error) {
	if (p_spv_bytes.size() < 20 || (p_spv_bytes.size() % 4) != 0) {
		r_error = "Invalid SPIR-V: too small or not aligned to 4 bytes";
		return {};
	}

	// Wrap in Godot-compatible Vector for the preprocessing API.
	Vector<uint8_t> spv;
	spv.resize((int64_t)p_spv_bytes.size());
	memcpy(spv.ptrw(), p_spv_bytes.data(), p_spv_bytes.size());

	// 21 preprocessing passes (same order as spirv_to_wgsl.cpp).
	spv = spirv_preprocess::inline_opaque_functions(spv);
	spv = spirv_preprocess::freeze_spec_constant_ops(spv);
	spv = spirv_preprocess::rewrite_copy_logical(spv);
	spv = spirv_preprocess::rewrite_terminate_invocation(spv);
	spv = spirv_preprocess::convert_push_constants_to_uniforms(spv);
	spv = spirv_preprocess::split_combined_samplers(spv);
	auto depth_result = spirv_preprocess::fix_depth2_images(spv);
	spv = depth_result.bytes;
	spv = spirv_preprocess::negate_position_y(spv);
	spv = spirv_preprocess::strip_unsupported_decorations(spv);
	spv = spirv_preprocess::strip_memory_barrier(spv);
	spv = spirv_preprocess::strip_image_write_operands(spv);
	spv = spirv_preprocess::strip_image_fetch_read_flag_only_operands(spv);
	spv = spirv_preprocess::split_initialized_local_arrays(spv);
	spv = spirv_preprocess::strip_helper_invocation_builtin(spv);
	spv = spirv_preprocess::fold_ballot_bit_count(spv);
	spv = spirv_preprocess::fix_nonfinite_literals(spv);
	spv = spirv_preprocess::flatten_binding_arrays(spv);
	spv = spirv_preprocess::infer_readonly_storage(spv);
	spv = spirv_preprocess::strip_writeonly_storage_decoration(spv);
	spv = spirv_preprocess::eliminate_dead_resources(spv);
	spv = spirv_preprocess::eliminate_local_single_block_vars(spv);

	// Debug: dump post-preprocessing SPIR-V when requested.
	if (const char *dump_path = getenv("TINT_DEBUG_DUMP_PREPROCESSED")) {
		std::ofstream out(dump_path, std::ios::binary);
		out.write((const char *)spv.ptr(), spv.size());
	}

	// Ensure SPIR-V version is at least 1.3 (0x00010300). The preprocessing
	// passes produce constructs (StorageBuffer storage class) that require 1.3,
	// but input SPIR-V may declare an older version in its header.
	if (spv.size() >= 20) {
		uint32_t version;
		memcpy(&version, spv.ptr() + 4, 4);
		if (version < 0x00010300) {
			version = 0x00010300;
			memcpy(spv.ptrw() + 4, &version, 4);
		}
	}

	// Convert to uint32_t words for Tint.
	size_t word_count = (size_t)spv.size() / 4;
	const uint32_t *words = reinterpret_cast<const uint32_t *>(spv.ptr());

	char *error_msg = nullptr;
	char *wgsl = tint_wrapper_spirv_to_wgsl(words, word_count, &error_msg);
	if (!wgsl) {
		r_error = error_msg ? error_msg : "Tint conversion failed (unknown error)";
		free(error_msg);
		return {};
	}

	std::string result(wgsl);
	free(wgsl);
	return result;
}

// Escape a string for JSON output (handles \, ", newlines, tabs).
//
// Also guarantees the result is valid UTF-8 by replacing every byte >= 0x80
// with '?': a Tint SPIR-V validation error can embed a raw disassembly dump
// (e.g. "spirv error: SPIR-V failed validation. | Expected Result Type and
// Operand type to be the same"), and that dump isn't guaranteed to be valid
// UTF-8 text. The caller (the Godot editor, shelling out to this tool for
// export-time shader baking) decodes this JSON with a strict UTF-8-aware
// String type; an invalid byte doesn't just get replaced there, it also
// logs a "Unicode parsing error" line for every single occurrence, which
// floods the export log for every shader hitting the same validation
// failure. Losing a few non-ASCII characters from an already-fallback
// diagnostic message is a fine trade for a non-spammed log.
// Truncate an error message to a reasonable length for logging. A SPIR-V
// validation failure's message embeds Tint's full disassembly of the module
// (easily thousands of lines) -- callers (the Godot editor, for export-time
// shader baking) just log this per failing shader, and a real project can
// have hundreds of them; printing the full disassembly every time balloons
// the log by hundreds of thousands of lines and makes a normal export look
// hung. Collapses newlines first so the truncated result stays one line.
static std::string truncate_error(const std::string &p_error) {
	std::string out;
	out.reserve(p_error.size());
	for (char c : p_error) {
		out += (c == '\n' || c == '\r') ? ' ' : c;
	}
	static constexpr size_t MAX_LEN = 300;
	if (out.size() > MAX_LEN) {
		out.resize(MAX_LEN);
		out += "... [truncated]";
	}
	return out;
}

static std::string json_escape(const std::string &p_str) {
	std::string out;
	out.reserve(p_str.size() + p_str.size() / 8);
	for (unsigned char c : p_str) {
		switch (c) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				if (c < 0x20 || c >= 0x80) {
					out += '?';
				} else {
					out += (char)c;
				}
				break;
		}
	}
	return out;
}

// Convert a single file in a child process. Tint can abort() on unhandled
// SPIR-V features (TINT_UNIMPLEMENTED); process isolation prevents one bad
// shader from killing the entire batch. POSIX forks; Windows has no fork(),
// so it re-runs this executable in --isolated-child mode instead (see
// run_isolated_child()).
//
// Both use the same result protocol: the child sends back one status byte
// ('W' = WGSL follows, 'E' = error message follows), then the payload.
//
// Returns WGSL on success, or sets r_error on failure.
#ifdef _WIN32
static std::string convert_isolated(const std::vector<uint8_t> &p_spv_bytes, std::string &r_error) {
	static wchar_t exe_path[32768];
	const DWORD exe_path_cap = (DWORD)(sizeof(exe_path) / sizeof(exe_path[0]));
	DWORD exe_path_len = GetModuleFileNameW(nullptr, exe_path, exe_path_cap);
	if (exe_path_len == 0 || exe_path_len >= exe_path_cap) {
		return convert_spirv_to_wgsl(p_spv_bytes, r_error);
	}

	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	// stdin carries the SPIR-V in, stdout carries the result back, stderr goes
	// to NUL so Tint's crash output can't reach this process's JSON stream.
	HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
	if (!CreatePipe(&in_read, &in_write, &sa, 0)) {
		return convert_spirv_to_wgsl(p_spv_bytes, r_error);
	}
	if (!CreatePipe(&out_read, &out_write, &sa, 0)) {
		CloseHandle(in_read);
		CloseHandle(in_write);
		return convert_spirv_to_wgsl(p_spv_bytes, r_error);
	}
	// Only the child's ends are inheritable.
	SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
	HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = in_read;
	si.hStdOutput = out_write;
	si.hStdError = nul;

	std::wstring cmd = L"\"" + std::wstring(exe_path) + L"\" --isolated-child";
	PROCESS_INFORMATION pi = {};
	BOOL created = CreateProcessW(exe_path, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

	// The child holds its own copies now; drop ours so reads see EOF when it exits.
	CloseHandle(in_read);
	CloseHandle(out_write);
	if (nul != INVALID_HANDLE_VALUE) {
		CloseHandle(nul);
	}

	if (!created) {
		CloseHandle(in_write);
		CloseHandle(out_read);
		return convert_spirv_to_wgsl(p_spv_bytes, r_error);
	}
	CloseHandle(pi.hThread);

	// The child reads all of stdin before writing anything, so sending the
	// whole input before reading can't deadlock on full pipe buffers. A write
	// failure just means the child already died; that's reported below.
	size_t written = 0;
	while (written < p_spv_bytes.size()) {
		DWORD chunk = 0;
		DWORD to_write = (DWORD)std::min<size_t>(p_spv_bytes.size() - written, 1 << 20);
		if (!WriteFile(in_write, p_spv_bytes.data() + written, to_write, &chunk, nullptr) || chunk == 0) {
			break;
		}
		written += chunk;
	}
	CloseHandle(in_write);

	std::string data;
	char buf[4096];
	DWORD n = 0;
	while (ReadFile(out_read, buf, sizeof(buf), &n, nullptr) && n > 0) {
		data.append(buf, (size_t)n);
	}
	CloseHandle(out_read);

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exit_code = 1;
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hProcess);

	if (exit_code != 0 || data.empty()) {
		r_error = "Tint crashed (likely TINT_UNIMPLEMENTED on unsupported SPIR-V feature)";
		return {};
	}

	if (data[0] == 'W') {
		return data.substr(1);
	} else {
		r_error = data.substr(1);
		return {};
	}
}

// Child side of convert_isolated() on Windows: SPIR-V on stdin, status byte +
// payload on stdout.
static int run_isolated_child() {
	// Runs unattended: a Tint abort() or crash should just end the process
	// with a non-zero exit code, not print an abort message or pop a Windows
	// Error Reporting dialog.
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

	_setmode(_fileno(stdin), _O_BINARY);
	std::vector<uint8_t> spv_bytes;
	static uint8_t buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
		spv_bytes.insert(spv_bytes.end(), buf, buf + n);
	}

	// Keep the real stdout as the result channel, then point the CRT's stdout
	// at NUL (stderr already is) so nothing Tint prints can corrupt it.
	int result_fd = _dup(_fileno(stdout));
	if (result_fd < 0 || !freopen("NUL", "w", stdout)) {
		return 1;
	}
	_setmode(result_fd, _O_BINARY);

	std::string err;
	std::string wgsl = convert_spirv_to_wgsl(spv_bytes, err);
	std::string out = wgsl.empty() ? "E" + err : "W" + wgsl;
	size_t written = 0;
	while (written < out.size()) {
		int w = _write(result_fd, out.data() + written, (unsigned int)std::min<size_t>(out.size() - written, 1 << 20));
		if (w <= 0) {
			return 1;
		}
		written += (size_t)w;
	}
	_close(result_fd);
	return 0;
}
#else
static std::string convert_isolated(const std::vector<uint8_t> &p_spv_bytes, std::string &r_error) {
	// Create a pipe for the child to send results back.
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		// Fallback: convert in-process if pipe fails.
		return convert_spirv_to_wgsl(p_spv_bytes, r_error);
	}

	// Flush parent's stdout before forking so the child doesn't
	// inherit any buffered data.
	fflush(stdout);
	std::cout.flush();

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return convert_spirv_to_wgsl(p_spv_bytes, r_error);
	}

	if (pid == 0) {
		// Child process.
		close(pipefd[0]); // Close read end.

		// Redirect stdout/stderr to /dev/null so Tint crash messages and
		// C++ runtime flush on abort() don't corrupt the parent's JSON
		// output stream. Don't use fclose() — it flushes the parent's
		// buffered cout data (copied on fork), duplicating output.
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}

		std::string err;
		std::string wgsl = convert_spirv_to_wgsl(p_spv_bytes, err);

		// Protocol: first byte is status ('W' = wgsl, 'E' = error).
		if (!wgsl.empty()) {
			char status = 'W';
			write(pipefd[1], &status, 1);
			write(pipefd[1], wgsl.data(), wgsl.size());
		} else {
			char status = 'E';
			write(pipefd[1], &status, 1);
			write(pipefd[1], err.data(), err.size());
		}
		close(pipefd[1]);
		_exit(0);
	}

	// Parent process.
	close(pipefd[1]); // Close write end.

	// Read all data from child.
	std::string data;
	char buf[4096];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
		data.append(buf, (size_t)n);
	}
	close(pipefd[0]);

	int status;
	waitpid(pid, &status, 0);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || data.empty()) {
		r_error = "Tint crashed (likely TINT_UNIMPLEMENTED on unsupported SPIR-V feature)";
		return {};
	}

	if (data[0] == 'W') {
		return data.substr(1);
	} else {
		r_error = data.substr(1);
		return {};
	}
}
#endif

static void print_usage() {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  tint_convert_cli <file.spv>                       Single file → WGSL to stdout\n");
	fprintf(stderr, "  tint_convert_cli --batch <file1.spv> [file2.spv]  Batch → JSON to stdout\n");
}

static int run(int argc, char *argv[]) {
	if (argc < 2) {
		print_usage();
		return 1;
	}

#ifdef _WIN32
	// Byte-for-byte output like POSIX (no "\n" -> "\r\n" translation).
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	tint_wrapper_initialize();

#ifdef _WIN32
	if (strcmp(argv[1], "--isolated-child") == 0) {
		return run_isolated_child();
	}
#endif

	bool batch_mode = (strcmp(argv[1], "--batch") == 0);

	if (batch_mode) {
		if (argc < 3) {
			fprintf(stderr, "Error: --batch requires at least one file argument.\n");
			return 1;
		}

		// Batch mode: output JSON { "path": "wgsl" | {"error": "msg"}, ... }
		std::cout << "{" << std::endl;
		for (int i = 2; i < argc; i++) {
			const char *path = argv[i];
			auto spv_bytes = read_file(path);

			std::cout << "  \"" << json_escape(path) << "\": ";

			if (spv_bytes.empty()) {
				std::cout << "{\"error\": \"Failed to read file\"}";
			} else {
				std::string error;
				std::string wgsl = convert_isolated(spv_bytes, error);
				if (wgsl.empty()) {
					std::cout << "{\"error\": \"" << json_escape(truncate_error(error)) << "\"}";
				} else {
					std::cout << "\"" << json_escape(wgsl) << "\"";
				}
			}

			if (i + 1 < argc) {
				std::cout << ",";
			}
			std::cout << std::endl;
		}
		std::cout << "}" << std::endl;
		return 0;

	} else {
		// Single file mode: output WGSL to stdout.
		const char *path = argv[1];
		auto spv_bytes = read_file(path);
		if (spv_bytes.empty()) {
			fprintf(stderr, "Error: Failed to read '%s'\n", path);
			return 1;
		}

		std::string error;
		std::string wgsl = convert_spirv_to_wgsl(spv_bytes, error);
		if (wgsl.empty()) {
			fprintf(stderr, "Error: %s\n", error.c_str());
			return 1;
		}

		std::cout << wgsl;
		return 0;
	}
}

#ifdef _WIN32
// Wide entry point so every argument arrives as UTF-8, whatever the ANSI code page.
int wmain(int argc, wchar_t *argv[]) {
	std::vector<std::string> args;
	args.reserve((size_t)argc);
	for (int i = 0; i < argc; i++) {
		args.push_back(wide_to_utf8(argv[i]));
	}
	std::vector<char *> args_ptrs;
	args_ptrs.reserve((size_t)argc + 1);
	for (std::string &arg : args) {
		args_ptrs.push_back(arg.data());
	}
	args_ptrs.push_back(nullptr);
	return run(argc, args_ptrs.data());
}
#else
int main(int argc, char *argv[]) {
	return run(argc, argv);
}
#endif
