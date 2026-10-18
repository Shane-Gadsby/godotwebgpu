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

} // namespace GDScriptTests
