/**************************************************************************/
/*  gdscript_formatter.h                                                  */
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
#include "core/templates/list.h"

// GDB: Formatter for GDScript (`.gd`) and braced GDScript (`.gdb`).
//
// It works on the token stream, with comments and whitespace recovered from the source text between tokens, so it
// never rebuilds code from the AST and cannot drop anything. Every result is checked before it is returned: the
// output must tokenize to the same tokens as the input and must still parse, otherwise the formatter reports an error
// and changes nothing.
//
// `.gdb` files are re-indented from their bracket structure. `.gd` files keep their block structure (indentation is
// syntax there); their indentation is only converted between tabs and spaces.
class GDScriptFormatter {
public:
	enum OperatorStyle {
		OPERATOR_STYLE_KEEP,
		OPERATOR_STYLE_WORDS, // `and`, `or`, `not`
		OPERATOR_STYLE_SYMBOLS, // `&&`, `||`, `!`
	};

	enum CommentStyle {
		COMMENT_STYLE_KEEP,
		COMMENT_STYLE_HASH, // `#`, `##`
		COMMENT_STYLE_SLASHES, // `//`, `///` (`.gdb` only)
	};

	struct Options {
		bool use_spaces = false;
		int indent_size = 4; // Spaces per level when `use_spaces`, and the width of a tab when reading `.gd` indentation.
		int paren_continuation = 2; // Levels added inside `( )`.
		int bracket_continuation = 1; // Levels added inside `[ ]`, dictionaries and enums.
		bool spaces_in_braces = false; // `{ "a": 1 }` instead of `{"a": 1}`.
		OperatorStyle operator_style = OPERATOR_STYLE_KEEP;
		CommentStyle comment_style = COMMENT_STYLE_KEEP;
		int blank_lines_around_members = 2; // Around functions and classes.
		int max_blank_lines = 1; // Anywhere else.
		bool insert_final_newline = true;
		bool use_crlf = false;
		int max_line_length = 100; // Not enforced yet.
	};

	// `"godot"`: the official GDScript style guide. `"js"`: K&R braces, spaces, symbols, `//` comments.
	static Options get_preset(const String &p_name);
	static String get_default_preset(bool p_braced);

	// Reads `.editorconfig` files from the file's directory upwards, as the EditorConfig specification describes.
	// Keys: `indent_style`, `indent_size`, `tab_width`, `insert_final_newline`, `end_of_line`, `max_line_length`, and the
	// `gdscript_*` keys documented in `get_editorconfig_keys()`.
	static Options get_options_for_path(const String &p_path, const String &p_preset_override = String());
	static String get_editorconfig_keys();

	struct Result {
		Error error = OK;
		String output;
		String message;
		int line = 0;
	};

	// `p_path` only decides brace mode (by extension) and the parser's script path.
	static Result format(const String &p_source, const String &p_path, const Options &p_options);

	// `godot --headless --gdscript-format [--check] [--preset <name>] [--indent-style tab|space] [--indent-size <n>]
	// [--stdin --stdin-path <path>] <paths>`. The indent flags override `.editorconfig`, so an editor can pass its own
	// effective settings. Returns the process exit code.
	static int run_cli(const List<String> &p_args);
};
