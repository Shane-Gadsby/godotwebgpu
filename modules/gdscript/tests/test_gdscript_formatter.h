/**************************************************************************/
/*  test_gdscript_formatter.h                                             */
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

// GDB: Formatter tests. This header is compiled into `tests/test_main.cpp`, where the module-local
// `GDSCRIPT_BRACES_ENABLED` is not defined, so `.gdb` cases check `is_braced_path()` at run time instead.

#include "tests/test_macros.h"

#include "modules/gdscript/gdscript_formatter.h"
#include "modules/gdscript/gdscript_tokenizer.h"

#ifdef TOOLS_ENABLED
#include "modules/gdscript/editor/gdscript_braced_conversion.h"
#include "modules/gdscript/editor/gdscript_editor_language.h"
#endif // TOOLS_ENABLED

namespace GDScriptTests {

static String _format_or_fail(const String &p_source, const String &p_path, const String &p_preset) {
	const GDScriptFormatter::Result result = GDScriptFormatter::format(p_source, p_path, GDScriptFormatter::get_preset(p_preset));
	CHECK_MESSAGE(result.error == OK, vformat("Formatting failed at line %d: %s", result.line, result.message));
	// Formatting is idempotent.
	const GDScriptFormatter::Result again = GDScriptFormatter::format(result.output, p_path, GDScriptFormatter::get_preset(p_preset));
	CHECK(again.error == OK);
	CHECK_MESSAGE(again.output == result.output, "Formatting the formatted code changed it again.");
	return result.output;
}

TEST_CASE("[Modules][GDScript] Formatter: .gd spacing, operators and member spacing (Godot preset)") {
	const String source =
			"extends Node\n"
			"var speed:int=10\n"
			"func a()->int:\n"
			"\tif speed>1 && !is_inside_tree():\n"
			"\t\treturn -speed\n"
			"\treturn speed%3\n"
			"func b(x:=2):\n"
			"\tprint( [1,2] , {\"k\":x} )\n";
	const String expected =
			"extends Node\n"
			"var speed: int = 10\n"
			"\n"
			"\n"
			"func a() -> int:\n"
			"\tif speed > 1 and not is_inside_tree():\n"
			"\t\treturn -speed\n"
			"\treturn speed % 3\n"
			"\n"
			"\n"
			"func b(x := 2):\n"
			"\tprint([1, 2], {\"k\": x})\n";
	CHECK(_format_or_fail(source, "res://a.gd", "godot") == expected);
}

TEST_CASE("[Modules][GDScript] Formatter: .gd indentation is converted, never restructured") {
	const String source =
			"func f():\n"
			"\tvar total = 0\n"
			"\tfor i in range(3):\n"
			"\t\ttotal += i\n"
			"\treturn total\n";
	const String expected =
			"func f():\n"
			"    var total = 0\n"
			"    for i in range(3):\n"
			"        total += i\n"
			"    return total\n";
	CHECK(_format_or_fail(source, "res://a.gd", "js") == expected);
}

TEST_CASE("[Modules][GDScript] Formatter: tokens are never merged or split") {
	// `- 2` is a minus and a number; `-2` would be one literal. `$A/B` and `%Unique` keep their spacing.
	const String source =
			"func f():\n"
			"\tprint(- 2, 3-2, -x(), $A/B, %Unique, 1 if x is not int else 2, x not in [1])\n";
	const String expected =
			"func f():\n"
			"\tprint(- 2, 3 - 2, -x(), $A/B, %Unique, 1 if x is not int else 2, x not in [1])\n";
	CHECK(_format_or_fail(source, "res://a.gd", "godot") == expected);
	// The symbols preset must not touch `is not` or `not in`.
	CHECK(_format_or_fail(source, "res://a.gd", "js").contains("x is not int else 2, x not in [1]"));
}

TEST_CASE("[Modules][GDScript] Formatter: refuses code that does not parse") {
	const GDScriptFormatter::Result result = GDScriptFormatter::format("func f(:\n\tpass\n", "res://a.gd", GDScriptFormatter::get_preset("godot"));
	CHECK(result.error != OK);
	CHECK(result.output.is_empty());
}

TEST_CASE("[Modules][GDScript] Formatter: braced GDScript (JS-like preset)") {
	if (!GDScriptTokenizer::is_braced_path("a.gdb")) {
		return; // Braced GDScript is compiled out.
	}

	SUBCASE("Indentation comes from braces, and multi-line blocks get their own lines") {
		const String source =
				"extends Node\n"
				"# A comment\n"
				"func _ready()->void{\n"
				"print(\"hi\")\n"
				"      if speed>5 and not ok(){\n"
				"  print( \"fast\" )\n"
				"}\n"
				"else{\n"
				"print(\"slow\")}\n"
				"for i in range(3){if i==1{continue}\n"
				"print(i)}\n"
				"}\n"
				"func helper(a:int,b:=2)->int{return a+b}\n";
		const String expected =
				"extends Node\n"
				"\n"
				"// A comment\n"
				"func _ready() -> void {\n"
				"    print(\"hi\")\n"
				"    if speed > 5 && !ok() {\n"
				"        print(\"fast\")\n"
				"    } else {\n"
				"        print(\"slow\")\n"
				"    }\n"
				"    for i in range(3) {\n"
				"        if i == 1 { continue }\n"
				"        print(i)\n"
				"    }\n"
				"}\n"
				"\n"
				"func helper(a: int, b := 2) -> int { return a + b }\n";
		CHECK(_format_or_fail(source, "res://a.gdb", "js") == expected);
	}

	SUBCASE("Lambdas in calls and dictionaries") {
		const String source =
				"func f(){\n"
				"print(apply(func(x){\n"
				"var r=x*x\n"
				"return r\n"
				"},6))\n"
				"var table={\"inc\":func(x){return x+1},\n"
				"\"dec\":func(x){\n"
				"return x-1\n"
				"},\n"
				"}\n"
				"}\n";
		const String expected =
				"func f() {\n"
				"    print(apply(func(x) {\n"
				"        var r = x * x\n"
				"        return r\n"
				"    }, 6))\n"
				"    var table = { \"inc\": func(x) { return x + 1 },\n"
				"        \"dec\": func(x) {\n"
				"            return x - 1\n"
				"        },\n"
				"    }\n"
				"}\n";
		CHECK(_format_or_fail(source, "res://a.gdb", "js") == expected);
	}

	SUBCASE("Godot preset on braced GDScript: tabs, words, double paren continuation") {
		const String source =
				"func f(){\n"
				"if a && b {\n"
				"call(1,\n"
				"2)\n"
				"}\n"
				"}\n";
		const String expected =
				"func f() {\n"
				"\tif a and b {\n"
				"\t\tcall(1,\n"
				"\t\t\t\t2)\n"
				"\t}\n"
				"}\n";
		CHECK(_format_or_fail(source, "res://a.gdb", "godot") == expected);
	}

	SUBCASE("Comment styles; region markers are kept") {
		const String source =
				"#region Setup\n"
				"## Documented.\n"
				"var x = 1 # trailing\n"
				"/* block\n"
				"   comment */\n"
				"#endregion\n";
		const String expected =
				"#region Setup\n"
				"/// Documented.\n"
				"var x = 1 // trailing\n"
				"/* block\n"
				"   comment */\n"
				"#endregion\n";
		CHECK(_format_or_fail(source, "res://a.gdb", "js") == expected);
	}

	SUBCASE("Refuses unbalanced braces") {
		const GDScriptFormatter::Result result = GDScriptFormatter::format("func f() {\n\tif x {\n}\n", "res://a.gdb", GDScriptFormatter::get_preset("js"));
		CHECK(result.error != OK);
		CHECK(result.message.contains("opened on line 1"));
	}
}

static String _format_with(const String &p_source, const String &p_path, const GDScriptFormatter::Options &p_options) {
	const GDScriptFormatter::Result result = GDScriptFormatter::format(p_source, p_path, p_options);
	CHECK_MESSAGE(result.error == OK, vformat("Formatting failed at line %d: %s", result.line, result.message));
	CHECK_MESSAGE(result.note.is_empty(), result.note);
	const GDScriptFormatter::Result again = GDScriptFormatter::format(result.output, p_path, p_options);
	CHECK_MESSAGE(again.output == result.output, "Formatting the formatted code changed it again.");
	return result.output;
}

TEST_CASE("[Modules][GDScript] Formatter: line wrapping, trailing commas and alignment") {
	if (GDScriptFormatter::get_braces_grammar_version() == 0) {
		return; // Uses braced GDScript, which this build does not have.
	}
	SUBCASE("A long line is wrapped at its last outermost list, with a trailing comma (JS-like preset)") {
		GDScriptFormatter::Options options = GDScriptFormatter::get_preset("js");
		options.max_line_length = 40;
		CHECK(_format_with("var stats = {\"health\": 100, \"mana\": 50, \"speed\": 3}\n", "res://a.gd", options) ==
				"var stats = {\n"
				"    \"health\": 100,\n"
				"    \"mana\": 50,\n"
				"    \"speed\": 3,\n"
				"}\n");
		CHECK(_format_with("func f() {\n    spawn(enemy_kind, spawn_position, 100)\n}\n", "res://a.gdb", options) ==
				"func f() {\n"
				"    spawn(\n"
				"        enemy_kind,\n"
				"        spawn_position,\n"
				"        100,\n"
				"    )\n"
				"}\n");
	}

	SUBCASE("The Godot preset wraps with its own continuation and only gives literals trailing commas") {
		GDScriptFormatter::Options options = GDScriptFormatter::get_preset("godot");
		options.max_line_length = 30;
		CHECK(_format_with("func f():\n\tspawn(enemy_kind, spawn_position)\n", "res://a.gd", options) ==
				"func f():\n"
				"\tspawn(\n"
				"\t\t\tenemy_kind,\n"
				"\t\t\tspawn_position\n"
				"\t)\n");
	}

	SUBCASE("A block written on one line that a wrap opens up gets its own lines") {
		GDScriptFormatter::Options options = GDScriptFormatter::get_preset("js");
		options.max_line_length = 30;
		CHECK(_format_with("func f() {\n    if ready { start(first_arg, second_arg) }\n}\n", "res://a.gdb", options) ==
				"func f() {\n"
				"    if ready {\n"
				"        start(\n"
				"            first_arg,\n"
				"            second_arg,\n"
				"        )\n"
				"    }\n"
				"}\n");
	}

	SUBCASE("Subscripts, grouping parentheses and lists holding a .gd lambda are not wrapped") {
		GDScriptFormatter::Options options = GDScriptFormatter::get_preset("godot");
		options.max_line_length = 10;
		CHECK(_format_with("var x = values[index_of_the_value]\n", "res://a.gd", options) == "var x = values[index_of_the_value]\n");
		CHECK(_format_with("var y = (first_value + second_value)\n", "res://a.gd", options) == "var y = (first_value + second_value)\n");
		CHECK(_format_with("var f = call_it(func(): return 1, 2)\n", "res://a.gd", options) == "var f = call_it(func(): return 1, 2)\n");
	}

	SUBCASE("Existing multi-line lists get trailing commas, but not after a rest pattern") {
		CHECK(_format_or_fail("var a = [\n\t1,\n\t2\n]\n", "res://a.gd", "godot") == "var a = [\n\t1,\n\t2,\n]\n");
		CHECK(_format_or_fail("func f():\n\tg(\n\t\t\t1\n\t)\n", "res://a.gd", "godot") == "func f():\n\tg(\n\t\t\t1\n\t)\n");
		CHECK(_format_or_fail("func f() {\n    g(\n        1\n    )\n}\n", "res://a.gdb", "js") == "func f() {\n    g(\n        1,\n    )\n}\n");
		CHECK(_format_or_fail("func f(v):\n\tmatch v:\n\t\t[\n\t\t\t1,\n\t\t\t..\n\t\t]:\n\t\t\tpass\n", "res://a.gd", "godot").contains("\t\t\t..\n"));
	}

	SUBCASE("Alignment, rule by rule") {
		GDScriptFormatter::Options options = GDScriptFormatter::get_preset("js");
		options.align_assignments = true;
		options.align_comments = true;
		CHECK(_format_with("var a = 1 // one\nvar long_name := 2 // two\nconst C = 3\n", "res://a.gdb", options) ==
				"var a         = 1  // one\n"
				"var long_name := 2 // two\n"
				"const C       = 3\n");
		options = GDScriptFormatter::get_preset("js");
		options.align_dictionary_values = true;
		CHECK(_format_with("var d = {\n    \"hp\": 1,\n    \"mana_points\": 2,\n}\n", "res://a.gdb", options) ==
				"var d = {\n    \"hp\":          1,\n    \"mana_points\": 2,\n}\n");
		options = GDScriptFormatter::get_preset("js");
		options.align_match_branches = true;
		CHECK(_format_with("func f(v) {\n    match v {\n        0 { return 0 }\n        100, 200 { return 1 }\n    }\n}\n", "res://a.gdb", options) ==
				"func f(v) {\n    match v {\n        0        { return 0 }\n        100, 200 { return 1 }\n    }\n}\n");
	}
}

TEST_CASE("[Modules][GDScript] Formatter: line ranges and diffs") {
	if (GDScriptFormatter::get_braces_grammar_version() == 0) {
		return; // Uses braced GDScript, which this build does not have.
	}
	const String source = "func a() {\nvar x=1\nvar y=2\n}\n\nfunc b() {\nvar z=3\n}\n";
	const GDScriptFormatter::Options options = GDScriptFormatter::get_preset("js");

	SUBCASE("Only the lines in the ranges change") {
		GDScriptFormatter::Result result = GDScriptFormatter::format_ranges(source, "res://a.gdb", options, { Vector2i(2, 2) });
		CHECK(result.error == OK);
		CHECK(result.output == "func a() {\n    var x = 1\nvar y=2\n}\n\nfunc b() {\nvar z=3\n}\n");
		result = GDScriptFormatter::format_ranges(source, "res://a.gdb", options, { Vector2i(6, 8), Vector2i(3, 3) });
		CHECK(result.output == "func a() {\nvar x=1\n    var y = 2\n}\n\nfunc b() {\n    var z = 3\n}\n");
	}

	SUBCASE("A range that would leave .gd indentation inconsistent is refused") {
		const String gd = "func f():\n    if true:\n        pass\n";
		GDScriptFormatter::Options godot = GDScriptFormatter::get_preset("godot");
		const GDScriptFormatter::Result result = GDScriptFormatter::format_ranges(gd, "res://a.gd", godot, { Vector2i(2, 2) });
		CHECK(result.error != OK);
	}

	SUBCASE("Unified diff") {
		const String after = GDScriptFormatter::format(source, "res://a.gdb", options).output;
		const String diff = GDScriptFormatter::unified_diff(source, after, "a.gdb");
		CHECK(diff.begins_with("--- a.gdb\n+++ a.gdb\n@@ -1,9 +1,9 @@\n func a() {\n-var x=1\n-var y=2\n+    var x = 1\n+    var y = 2\n"));
		CHECK(GDScriptFormatter::unified_diff(after, after, "a.gdb").is_empty());
	}
}

// GDScriptFormatter::convert() needs a debug or editor build.
static bool _converter_available() {
#ifdef DEBUG_ENABLED
	return true;
#else
	return false;
#endif
}

TEST_CASE("[Modules][GDScript] Formatter: cases found by fuzzing") {
	if (GDScriptFormatter::get_braces_grammar_version() == 0) {
		return; // Uses braced GDScript, which this build does not have.
	}
	SUBCASE("A file without a final line break") {
		CHECK(_format_or_fail("var a = b", "res://a.gd", "godot") == "var a = b\n");
		CHECK(_format_or_fail("func f() {\n}\nvar d = { \"a\": 1 }", "res://a.gdb", "js") == "func f() {\n}\n\nvar d = { \"a\": 1 }\n");
		if (_converter_available()) {
			const GDScriptFormatter::Result converted = GDScriptFormatter::convert("var p = n ? 1 : 2", "res://a.gdb", false, GDScriptFormatter::get_preset("godot"));
			CHECK(converted.error == OK);
			CHECK(converted.output == "var p = 1 if n else 2\n");
		}
	}

	SUBCASE("A respelled operator never touches a word") {
		CHECK(_format_or_fail("func f(v):\n\treturn \"a\" if v else! \"b\"\n", "res://a.gd", "godot") == "func f(v):\n\treturn \"a\" if v else not \"b\"\n");
	}

	SUBCASE("A statement after a `}` gets its own line, and the blocks around it span lines") {
		CHECK(_format_or_fail("func f(x) {\n\tif x { if x { g() } h() }\n}\n", "res://a.gdb", "js") == "func f(x) {\n    if x {\n        if x { g() }\n        h()\n    }\n}\n");
		CHECK(_format_or_fail("func f() {\n}func g() {\n}\n", "res://a.gdb", "js") == "func f() {\n}\n\nfunc g() {\n}\n");
	}
}

TEST_CASE("[Modules][GDScript] Converter between .gd and .gdb") {
	if (!GDScriptTokenizer::is_braced_path("a.gdb")) {
		return; // Braced GDScript is compiled out.
	}
	if (!_converter_available()) {
		return; // A release template runs the tests too.
	}

	SUBCASE(".gd to .gdb keeps every line") {
		const String source =
				"class Inner extends RefCounted:\n"
				"\tvar p: int:\n"
				"\t\tget:\n"
				"\t\t\treturn 1\n"
				"\n"
				"\n"
				"func f(x):\n"
				"\tif x: print(1)\n"
				"\telif x == 2:\n"
				"\t\tprint(2) # two\n"
				"\telse:\n"
				"\t\tmatch x:\n"
				"\t\t\t_:\n"
				"\t\t\t\tpass\n"
				"\tvar arr = [1].map(func(v): return v * 2)\n"
				"\treturn arr\n";
		const String expected =
				"class Inner extends RefCounted {\n"
				"\tvar p: int {\n"
				"\t\tget {\n"
				"\t\t\treturn 1 } } }\n"
				"\n"
				"\n"
				"func f(x) {\n"
				"\tif x { print(1) }\n"
				"\telif x == 2 {\n"
				"\t\tprint(2) } # two\n"
				"\telse {\n"
				"\t\tmatch x {\n"
				"\t\t\t_ {\n"
				"\t\t\t\tpass } } }\n"
				"\tvar arr = [1].map(func(v) { return v * 2 })\n"
				"\treturn arr }\n";
		const GDScriptFormatter::Result result = GDScriptFormatter::convert(source, "res://a.gd", true, GDScriptFormatter::get_preset("js"));
		CHECK_MESSAGE(result.error == OK, result.message);
		CHECK(result.output == expected);
	}

	SUBCASE(".gdb to .gd") {
		const String source =
				"func f(x) {\n"
				"    if x { print(1) } else if x == 2 { print(2) }\n"
				"    if x { if x { print(3) } }\n"
				"    var fns = [\n"
				"        func() {\n"
				"            return 1 },\n"
				"    ]\n"
				"    var y = 1 /* inline */ + 2 // trailing\n"
				"    var empty = func() {}\n"
				"    return [fns, y, empty]\n"
				"}\n";
		const String expected =
				"func f(x):\n"
				"\tif x: print(1)\n"
				"\telif x == 2: print(2)\n"
				"\tif x:\n"
				"\t\tif x: print(3)\n"
				"\tvar fns = [\n"
				"\t\tfunc():\n"
				"\t\t\treturn 1,\n"
				"\t]\n"
				"\tvar y = 1 + 2 # trailing  # inline\n"
				"\tvar empty = func(): pass\n"
				"\treturn [fns, y, empty]\n";
		const GDScriptFormatter::Result result = GDScriptFormatter::convert(source, "res://a.gdb", false, GDScriptFormatter::get_preset("godot"));
		CHECK_MESSAGE(result.error == OK, result.message);
		CHECK(result.output == expected);
	}

	SUBCASE("Refuses code that does not parse") {
		CHECK(GDScriptFormatter::convert("func f(:\n", "res://a.gd", true, GDScriptFormatter::get_preset("godot")).error != OK);
		CHECK(GDScriptFormatter::convert("func f() {\n", "res://a.gdb", false, GDScriptFormatter::get_preset("godot")).error != OK);
	}
}

#ifdef TOOLS_ENABLED
TEST_CASE("[Modules][GDScript] Braced GDScript documentation comments") {
	if (!GDScriptTokenizer::is_braced_path("a.gdb")) {
		return; // Braced GDScript is compiled out.
	}
	const String code =
			"/** A braced class. */\n"
			"extends Node\n"
			"\n"
			"/**\n"
			" * Adds one.\n"
			" * @deprecated: Use add_two.\n"
			" */\n"
			"func add_one(x: int) -> int { return x + 1 }\n"
			"\n"
			"/// Doubles.\n"
			"func double(x: int) -> int { return x * 2 }\n";
	GDScriptParser parser;
	REQUIRE(parser.parse(code, "res://docs.gdb", false) == OK);
	const GDScriptParser::ClassNode *head = parser.get_tree();
	CHECK(head->doc_data.brief == "A braced class.");
	const GDScriptParser::FunctionNode *add_one = head->get_member("add_one").function;
	CHECK(add_one->doc_data.description == "Adds one.");
	CHECK(add_one->doc_data.is_deprecated);
	CHECK(add_one->doc_data.deprecated_message == "Use add_two.");
	CHECK(head->get_member("double").function->doc_data.description == "Doubles.");

	// Converting to `.gd` keeps them as `##` comments.
	const GDScriptFormatter::Result result = GDScriptFormatter::convert(code, "res://docs.gdb", false, GDScriptFormatter::get_preset("godot"));
	REQUIRE(result.error == OK);
	CHECK(result.output.contains("## Adds one.\n## @deprecated: Use add_two.\nfunc add_one(x: int) -> int:"));
}

TEST_CASE("[Modules][GDScript] Script editor hooks for braced GDScript") {
	const GDScriptEditorLanguage *language = GDScriptEditorLanguage::get_singleton();
	REQUIRE(language != nullptr);

	// `.gd` keeps the language's own behavior.
	CHECK(language->get_comment_delimiters_for_path("res://a.gd").is_empty());
	CHECK_FALSE(language->uses_brace_blocks("res://a.gd"));
	CHECK(language->can_reformat_code("res://a.gd"));

	if (!GDScriptTokenizer::is_braced_path("a.gdb")) {
		return; // Braced GDScript is compiled out.
	}

	CHECK(language->get_comment_delimiters_for_path("res://a.gdb")[0] == "//");
	CHECK(language->get_doc_comment_delimiters_for_path("res://a.gdb")[0] == "///");
	CHECK(language->uses_brace_blocks("res://a.gdb"));

	SUBCASE("Auto Indent follows braces and skips strings and comments") {
		String code =
				"func f() {\n"
				"        if x {\n"
				"a(\"{\") // {\n"
				"  }\n"
				"/* { */ b(\n"
				"c)\n"
				"}\n";
		language->format_code_for_path(code, 0, 6, "res://a.gdb");
		CHECK(code ==
				"func f() {\n"
				"\tif x {\n"
				"\t\ta(\"{\") // {\n"
				"\t}\n"
				"\t/* { */ b(\n"
				"\t\tc)\n"
				"}\n");
	}

	SUBCASE("Path strings follow a conversion") {
		HashMap<String, String> renames;
		renames["res://scripts/a.gd"] = "res://scripts/a.gdb";
		String code =
				"const A = preload(\"res://scripts/a.gd\")\n"
				"const B = preload(\"a.gd\")\n"
				"const C = preload('../scripts/a.gd')\n"
				"var other = \"res://scripts/b.gd\" # \"res://scripts/a.gd\" in a comment stays\n";
		CHECK(GDScriptBracedConversion::update_path_strings(code, "res://scripts/user.gd", renames) == 3);
		CHECK(code ==
				"const A = preload(\"res://scripts/a.gdb\")\n"
				"const B = preload(\"a.gdb\")\n"
				"const C = preload('../scripts/a.gdb')\n" // Kept as written; only the extension changes.
				"var other = \"res://scripts/b.gd\" # \"res://scripts/a.gd\" in a comment stays\n");
	}

	SUBCASE("Templates follow the file they create") {
		const String indented = "extends Node\n\n\nfunc _ready() -> void:\n\tpass\n";
		const String braced = language->adapt_template_for_path(indented, "res://new.gdb");
		CHECK(braced.contains("func _ready() -> void {"));
		CHECK(language->adapt_template_for_path(indented, "res://new.gd") == indented);
		CHECK(language->adapt_template_for_path(braced, "res://new.gd").contains("func _ready() -> void:"));
	}

	SUBCASE("Reformat Code") {
		String formatted;
		int error_line = 0;
		String error;
		CHECK(language->reformat_code("func f(){\nif a{b()}\n}\n", "res://a.gdb", formatted, error_line, error) == OK);
		// The indentation depends on whichever `.editorconfig` applies to `res://`, so only the layout is checked.
		const Vector<String> lines = formatted.split("\n");
		REQUIRE(lines.size() == 4);
		CHECK(lines[0] == "func f() {");
		CHECK(lines[1].strip_edges() == "if a { b() }");
		CHECK(lines[1] != lines[1].strip_edges()); // Indented.
		CHECK(lines[2] == "}");
		CHECK(language->reformat_code("func f(){\n", "res://a.gdb", formatted, error_line, error) != OK);
		CHECK(error_line == 1);
	}

	SUBCASE("Reformat Code on selected lines") {
		String formatted;
		int error_line = 0;
		String error;
		CHECK(language->reformat_code("func f() {\nvar x=1\nvar y=2\n}\n", "res://a.gdb", formatted, error_line, error, { Vector2i(3, 3) }) == OK);
		const Vector<String> lines = formatted.split("\n");
		REQUIRE(lines.size() == 5);
		CHECK(lines[1] == "var x=1"); // Not selected.
		CHECK(lines[2].strip_edges() == "var y = 2");
	}
}
#endif // TOOLS_ENABLED

} // namespace GDScriptTests
