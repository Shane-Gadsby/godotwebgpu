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

TEST_CASE("[Modules][GDScript] Converter between .gd and .gdb") {
	if (!GDScriptTokenizer::is_braced_path("a.gdb")) {
		return; // Braced GDScript is compiled out.
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
}
#endif // TOOLS_ENABLED

} // namespace GDScriptTests
