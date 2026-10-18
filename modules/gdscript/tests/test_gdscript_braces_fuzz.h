/**************************************************************************/
/*  test_gdscript_braces_fuzz.h                                           */
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

// GDB: Fuzzing of braced GDScript. Mangled `.gdb` sources go through the tokenizer, the parser, the formatter and the
// converter, which must neither crash nor hang, and whose formatter output must be stable. Deterministic (fixed seed);
// `GDB_FUZZ_ITERATIONS` runs more inputs, `GDB_FUZZ_SEED` other ones, and `GDB_FUZZ_DUMP=<file>` writes each input
// there before processing it, so a crash leaves it behind; `GDB_FUZZ_FAILURES=<folder>` keeps each failing input.

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/math/random_pcg.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

#include "modules/gdscript/gdscript_formatter.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_tokenizer.h"

namespace GDScriptTests {

static String _fuzz_mutate(const String &p_source, RandomPCG &r_rng) {
	static const String alphabet = String::utf8("{}()[]:;,=>?!&|/*#\"'\\ \t\n.$%@-+0aZ_é");
	String text = p_source;
	const int operations = 1 + r_rng.rand() % 4;
	for (int op = 0; op < operations; op++) {
		const int length = text.length();
		const int at = length > 0 ? (int)(r_rng.rand() % (length + 1)) : 0;
		switch (r_rng.rand() % 6) {
			case 0: // Insert a character.
				text = text.insert(at, String::chr(alphabet[r_rng.rand() % alphabet.length()]));
				break;
			case 1: // Delete a run.
				if (length > 0) {
					text = text.substr(0, at) + text.substr(MIN(length, at + 1 + (int)(r_rng.rand() % 8)));
				}
				break;
			case 2: { // Duplicate a slice somewhere else.
				const int from = length > 0 ? (int)(r_rng.rand() % length) : 0;
				const String slice = text.substr(from, 1 + r_rng.rand() % 40);
				text = text.insert(at, slice);
			} break;
			case 3: { // Swap two lines.
				Vector<String> lines = text.split("\n");
				if (lines.size() > 1) {
					const int a = r_rng.rand() % lines.size();
					const int b = r_rng.rand() % lines.size();
					const String line = lines[a];
					lines.write[a] = lines[b];
					lines.write[b] = line;
					text = String("\n").join(lines);
				}
			} break;
			case 4: // Truncate.
				text = text.substr(0, at);
				break;
			default: { // Replace a bracket with another kind.
				static const String brackets = "{}()[]";
				for (int tries = 0; tries < 20 && length > 0; tries++) {
					const int position = r_rng.rand() % length;
					if (brackets.contains_char(text[position])) {
						text = text.substr(0, position) + String::chr(brackets[r_rng.rand() % brackets.length()]) + text.substr(position + 1);
						break;
					}
				}
			} break;
		}
	}
	return text;
}

TEST_CASE("[Modules][GDScript] Braced GDScript survives mangled input") {
	// A test header is compiled without the module's defines, so this checks for brace mode at run time.
	if (GDScriptFormatter::get_braces_grammar_version() == 0) {
		return;
	}
	// The braced feature and error tests, their indented twins, and a few edge cases, are the seeds.
	Vector<String> seeds;
	Vector<bool> seed_braced;
	for (const String &folder : { String("modules/gdscript/tests/scripts/braces/features"), String("modules/gdscript/tests/scripts/braces/errors") }) {
		Ref<DirAccess> dir = DirAccess::open(folder);
		if (dir.is_null()) {
			continue;
		}
		for (const String &name : dir->get_files()) {
			if (name.ends_with(".gdb") || name.ends_with(".gd")) {
				seeds.push_back(FileAccess::get_file_as_string(folder.path_join(name)));
				seed_braced.push_back(name.ends_with(".gdb"));
			}
		}
	}
	seeds.push_back("func f() {\n\tvar d = { \"a\": func(x) { return x ? 1 : 2 } }\n\treturn (x) => { return d }\n}\n");
	seeds.push_back("/** doc */\nenum E { A, B, }\nmatch v {\n\t[1, ..] { pass }\n\t_ {}\n}\n");
	seeds.push_back("");
	seed_braced.push_back(true);
	seed_braced.push_back(true);
	seed_braced.push_back(true);

	const String iterations_env = OS::get_singleton()->get_environment("GDB_FUZZ_ITERATIONS");
	const String seed_env = OS::get_singleton()->get_environment("GDB_FUZZ_SEED");
	const int iterations = iterations_env.is_valid_int() ? iterations_env.to_int() : 300;
	RandomPCG rng(seed_env.is_valid_int() ? seed_env.to_int() : 20261018);

	const GDScriptFormatter::Options options = GDScriptFormatter::get_preset("js");
	int internal_errors = 0;
	String first_internal;
	const String dump = OS::get_singleton()->get_environment("GDB_FUZZ_DUMP");
	const String failures = OS::get_singleton()->get_environment("GDB_FUZZ_FAILURES");
	auto keep = [&](const String &p_kind, int p_index, const String &p_input) {
		if (failures.is_empty()) {
			return;
		}
		Ref<FileAccess> f = FileAccess::open(failures.path_join(vformat("%s_%d.gdb", p_kind, p_index)), FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(p_input);
		}
	};
	for (int i = 0; i < iterations; i++) {
		const int seed = rng.rand() % seeds.size();
		const bool braced = seed_braced[seed];
		const String path = braced ? "res://fuzz.gdb" : "res://fuzz.gd";
		const String input = _fuzz_mutate(seeds[seed], rng);
		if (!dump.is_empty()) {
			// The input being processed, so a crash leaves it behind.
			Ref<FileAccess> f = FileAccess::open(dump, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_string(input);
			}
		}

		// The tokenizer ends.
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code(input);
		tokenizer.set_brace_mode(braced);
		tokenizer.set_multiline_mode(rng.rand() % 2 == 0);
		const int limit = input.length() * 4 + 100;
		int count = 0;
		while (count < limit && tokenizer.scan().type != GDScriptTokenizer::Token::TK_EOF) {
			count++;
		}
		CHECK_MESSAGE(count < limit, vformat("The tokenizer did not reach the end of input %d:\n%s", i, input));

		// The parser, the formatter and the converter return.
		GDScriptParser parser;
		parser.parse(input, path, false);
		const GDScriptFormatter::Result formatted = GDScriptFormatter::format(input, path, braced ? options : GDScriptFormatter::get_preset("godot"));
		if (formatted.error == OK) {
			const GDScriptFormatter::Result again = GDScriptFormatter::format(formatted.output, path, braced ? options : GDScriptFormatter::get_preset("godot"));
			CHECK_MESSAGE(again.output == formatted.output, vformat("Formatting is not stable for input %d:\n%s", i, input));
			if (again.output != formatted.output) {
				keep(braced ? "unstable" : "unstable_gd", i, input);
			}
			GDScriptFormatter::convert(input, path, false, GDScriptFormatter::get_preset(braced ? "godot" : "js"));
		}
		if (!formatted.note.is_empty() || formatted.message.begins_with("Internal")) {
			keep(braced ? "internal" : "internal_gd", i, input);
			internal_errors++;
			if (first_internal.is_empty()) {
				first_internal = vformat("%s\n%s", formatted.note.is_empty() ? formatted.message : formatted.note, input);
			}
		}
	}
	CHECK_MESSAGE(internal_errors == 0, vformat("%d inputs hit an internal formatter error; the first:\n%s", internal_errors, first_internal));
}

} // namespace GDScriptTests
