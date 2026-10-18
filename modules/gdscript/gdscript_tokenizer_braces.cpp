/**************************************************************************/
/*  gdscript_tokenizer_braces.cpp                                         */
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

// GDB: Brace mode for braced GDScript (`.gdb`) files.
//
// The parser is not aware of braces. In brace mode, the tokenizer ignores leading whitespace and translates the
// braces of a block into the tokens an indented block produces:
//
//   block-opening "{"   ->  COLON NEWLINE INDENT
//   block-closing "}"   ->  NEWLINE DEDENT          (PASS NEWLINE DEDENT for an empty block)
//
// so `if x { a() }` reaches the parser exactly as `if x:` followed by an indented `a()` would.
//
// Whether a "{" opens a block or a dictionary depends only on the token before it (the D3 rule of the plan):
//
//   at the start of a statement                         -> dictionary (a match pattern, or an expression)
//   after `enum` or `enum Name`                         -> enum body
//   after a statement-level `else` (after "}" or a line break)    -> block
//   after a token that can end an expression or type
//     (identifier, literal, ")", "]", "}", `self`, `void`, `_`, PI/TAU/INF/NAN)   -> block
//   anything else ("=", "(", ",", "[", ":", `return`, an operator, ...)           -> dictionary
//
// GDScript has no construct where a dictionary literal directly follows a complete expression, which is what
// makes the rule unambiguous. A "{" on the line after a block header (Allman style) is an error, reported before the
// parser can trip over the missing block.
//
// Brace mode also accepts `//` line comments, `///` documentation comments and `/* */` block comments, next to `#`,
// `else if` as a spelling of `elif`, and arrow lambdas: `(a, b) => a + b`, `x => x * 2` and `(a) => { ... }` are
// `func(a, b): return a + b`, `func(x): return x * 2` and `func(a) { ... }`.

#include "gdscript_tokenizer.h"

#include "core/string/char_utils.h"

#ifdef GDSCRIPT_BRACES_ENABLED

static bool _brace_is_block_head(GDScriptTokenizer::Token::Type p_type) {
	switch (p_type) {
		case GDScriptTokenizer::Token::IF:
		case GDScriptTokenizer::Token::ELIF:
		case GDScriptTokenizer::Token::ELSE:
		case GDScriptTokenizer::Token::FOR:
		case GDScriptTokenizer::Token::WHILE:
		case GDScriptTokenizer::Token::FUNC:
		case GDScriptTokenizer::Token::STATIC:
		case GDScriptTokenizer::Token::CLASS:
		case GDScriptTokenizer::Token::MATCH:
			return true;
		default:
			return false;
	}
}

static bool _brace_can_end_header(GDScriptTokenizer::Token::Type p_type) {
	switch (p_type) {
		case GDScriptTokenizer::Token::TK_VOID: // `func f() -> void {`
		case GDScriptTokenizer::Token::UNDERSCORE: // Wildcard match pattern `_ {`.
			return true;
		default:
			return GDScriptTokenizer::Token(p_type).can_precede_bin_op();
	}
}

GDScriptTokenizer::Token GDScriptTokenizerText::_brace_make_synthetic(Token::Type p_type, int p_line, int p_column) const {
	Token token(p_type);
	token.start_line = p_line;
	token.end_line = p_line;
	token.start_column = p_column;
	token.end_column = p_column + 1;
	return token;
}

GDScriptTokenizer::Token GDScriptTokenizerText::_brace_scan() {
	Token token;
	if (!brace_queue.is_empty()) {
		token = brace_queue.front()->get();
		brace_queue.pop_front();
	} else {
		brace_in_scan = true;
		token = scan();
		brace_in_scan = false;

		if (token.type == Token::IDENTIFIER && _brace_arrow_at(0)) {
			// `x => ...`: a lambda with one parameter, `func(x) ...`.
			brace_queue.push_back(_brace_make_synthetic(Token::PARENTHESIS_OPEN, token.start_line, token.start_column));
			brace_queue.push_back(token);
			brace_queue.push_back(_brace_make_synthetic(Token::PARENTHESIS_CLOSE, token.end_line, token.end_column));
			token = _brace_make_synthetic(Token::FUNC, token.start_line, token.start_column);
		}

		if (token.type == Token::ELSE && brace_line_start) {
			// `else if` after a block is `elif`. Only a statement-level `else` merges; the `else` of `a if c else b`
			// never starts a line.
			int offset = 0;
			while (_peek(offset) == ' ' || _peek(offset) == '\t') {
				offset++;
			}
			if (_peek(offset) == 'i' && _peek(offset + 1) == 'f' && !is_unicode_identifier_continue(_peek(offset + 2))) {
				String merged = token.source;
				for (int i = 0; i < offset + 2; i++) {
					merged += _advance();
				}
				token.type = Token::ELIF;
				token.source = merged;
				token.end_line = line;
				token.end_column = column;
			}
		}

		if (token.type == Token::NEWLINE && _brace_is_block_head(brace_line_head) && _peek() == '{') {
			// Allman style: the block's "{" is on the next line. The scanner already skipped to it, so report the error
			// and drop the line break, which leaves the parser an ordinary `header {` and no cascade of errors.
			Token error = make_error(R"(A "{" that opens a block must be on the same line as the statement it belongs to, as in "if x {". Move it to the end of the previous line.)");
			error.start_line = line;
			error.end_line = line;
			error.start_column = column;
			error.end_column = column + 1;
			return error;
		}

		if (token.type == Token::TK_EOF && !brace_stack.is_empty()) {
			// Close every block that is still open, so the parser can finish, and report the innermost opener.
			const BraceEntry &innermost = brace_stack.back()->get();
			Token error = make_error(vformat(R"(Missing "}" to close the "{" opened on line %d.)", innermost.line));
			error.start_line = innermost.line;
			error.end_line = innermost.line;
			error.start_column = innermost.column;
			error.end_column = innermost.column + 1;

			bool need_newline = !brace_line_start;
			for (List<BraceEntry>::Element *E = brace_stack.back(); E; E = E->prev()) {
				if (E->get().kind != BRACE_BLOCK) {
					continue;
				}
				if (need_newline) {
					brace_queue.push_back(_brace_make_synthetic(Token::NEWLINE, token.start_line, token.start_column));
					need_newline = false;
				}
				brace_queue.push_back(_brace_make_synthetic(Token::DEDENT, token.start_line, token.start_column));
			}
			brace_queue.push_back(token);
			brace_stack.clear();
			return error;
		}
	}

	switch (token.type) {
		case Token::NEWLINE:
		case Token::INDENT:
		case Token::DEDENT:
			brace_line_start = true;
			brace_line_head = Token::EMPTY;
			break;
		case Token::EMPTY:
		case Token::ERROR:
		case Token::TK_EOF:
			break;
		default:
			if (token.type == Token::ELSE) {
				brace_else_at_line_start = brace_line_start;
			}
			if (brace_line_start) {
				brace_line_head = token.type;
			}
			brace_line_start = false;
			brace_prev_prev_type = brace_prev_type;
			brace_prev_type = token.type;
			brace_token_count++;
			break;
	}
	return token;
}

GDScriptTokenizer::Token GDScriptTokenizerText::_brace_open() {
	BraceEntry entry;
	entry.line = start_line;
	entry.column = start_column;
	entry.paren_depth = paren_stack.size();
	entry.tokens_at_open = brace_token_count;

	if (brace_line_start) {
		entry.kind = BRACE_DICTIONARY;
	} else if (brace_prev_type == Token::ENUM || (brace_prev_type == Token::IDENTIFIER && brace_prev_prev_type == Token::ENUM)) {
		entry.kind = BRACE_ENUM;
	} else if (brace_prev_type == Token::ELSE) {
		entry.kind = brace_else_at_line_start ? BRACE_BLOCK : BRACE_DICTIONARY;
	} else if (_brace_can_end_header(brace_prev_type)) {
		entry.kind = BRACE_BLOCK;
	} else {
		entry.kind = BRACE_DICTIONARY;
	}

	brace_stack.push_back(entry);

	if (entry.kind != BRACE_BLOCK) {
		push_paren('{');
		return make_token(Token::BRACE_OPEN);
	}

	brace_queue.push_back(_brace_make_synthetic(Token::NEWLINE, entry.line, entry.column));
	brace_queue.push_back(_brace_make_synthetic(Token::INDENT, entry.line, entry.column));
	return make_token(Token::COLON);
}

GDScriptTokenizer::Token GDScriptTokenizerText::_brace_close() {
	if (brace_stack.is_empty()) {
		return make_error(R"(Closing "}" doesn't have an opening counterpart.)");
	}

	const BraceEntry entry = brace_stack.back()->get();
	brace_stack.pop_back();

	if (entry.kind != BRACE_BLOCK) {
		if (!pop_paren('{')) {
			return make_paren_error('}');
		}
		return make_token(Token::BRACE_CLOSE);
	}

	Token paren_error;
	if (paren_stack.size() > entry.paren_depth) {
		// A "(" or "[" opened inside the block was never closed. Report it, and drop it so the block can still close.
		paren_error = make_error(vformat(R"(Closing "}" found while "%c" is still open.)", paren_stack.back()->get()));
		while (paren_stack.size() > entry.paren_depth) {
			paren_stack.pop_back();
		}
	}

	// +1: the COLON returned for the opening brace.
	const bool empty = brace_token_count == entry.tokens_at_open + 1;
	if (empty) {
		// `{}` is an empty block, which an indented block cannot express without `pass`.
		brace_queue.push_back(_brace_make_synthetic(Token::PASS, start_line, start_column));
		brace_queue.push_back(_brace_make_synthetic(Token::NEWLINE, start_line, start_column));
	} else if (!brace_line_start) {
		brace_queue.push_back(_brace_make_synthetic(Token::NEWLINE, start_line, start_column));
	}
	brace_queue.push_back(_brace_make_synthetic(Token::DEDENT, start_line, start_column));

	if (paren_error.type == Token::ERROR) {
		return paren_error;
	}
	Token first = brace_queue.front()->get();
	brace_queue.pop_front();
	return first;
}

GDScriptTokenizer::Token GDScriptTokenizerText::_brace_check_paren_close(char32_t p_paren) {
	if (brace_stack.is_empty()) {
		return Token();
	}
	const BraceEntry &entry = brace_stack.back()->get();
	if (entry.kind == BRACE_BLOCK && entry.paren_depth >= paren_stack.size()) {
		return make_error(vformat(R"(Closing "%c" found while the block opened by "{" on line %d is still open.)", p_paren, entry.line));
	}
	return Token();
}

bool GDScriptTokenizerText::_brace_arrow_at(int p_offset) const {
	// `=>` after optional spaces, at `_current[p_offset]`.
	int i = p_offset;
	while (position + i < length && (_current[i] == ' ' || _current[i] == '\t')) {
		i++;
	}
	return position + i + 1 < length && _current[i] == '=' && _current[i + 1] == '>';
}

bool GDScriptTokenizerText::_brace_arrow_after_paren() {
	// Whether the "(" just read starts an arrow lambda's parameters: its ")" is followed by `=>`. Strings, comments and
	// nested brackets are skipped; the scan stops at the end of a statement-ending line outside brackets.
	int depth = 1;
	int i = 0;
	while (position + i < length) {
		const char32_t c = _current[i];
		if (c == '"' || c == '\'') {
			const char32_t quote = c;
			const bool triple = position + i + 2 < length && _current[i + 1] == quote && _current[i + 2] == quote;
			i += triple ? 3 : 1;
			while (position + i < length) {
				if (_current[i] == '\\') {
					i += 2;
					continue;
				}
				if (_current[i] == quote && (!triple || (position + i + 2 < length && _current[i + 1] == quote && _current[i + 2] == quote))) {
					i += triple ? 3 : 1;
					break;
				}
				i++;
			}
			continue;
		}
		if (c == '#' || (c == '/' && position + i + 1 < length && _current[i + 1] == '/')) {
			while (position + i < length && _current[i] != '\n') {
				i++;
			}
			continue;
		}
		if (c == '/' && position + i + 1 < length && _current[i + 1] == '*') {
			i += 2;
			while (position + i + 1 < length && !(_current[i] == '*' && _current[i + 1] == '/')) {
				i++;
			}
			i += 2;
			continue;
		}
		if (c == '(' || c == '[' || c == '{') {
			depth++;
		} else if (c == ')' || c == ']' || c == '}') {
			depth--;
			if (depth == 0) {
				return _brace_arrow_at(i + 1);
			}
		}
		i++;
	}
	return false;
}

GDScriptTokenizer::Token GDScriptTokenizerText::_brace_arrow() {
	// After `(a) =>` or `x =>`. Before a "{" the arrow adds nothing: the block is the lambda's body. Otherwise the
	// body is one expression, returned: a COLON, then a RETURN whose text is the `=>` itself, so tools that map tokens
	// back to the source find the arrow where it was written.
	_advance(); // ">"
	Token arrow = make_token(Token::RETURN);
	int i = 0;
	while (position + i < length && (_current[i] == ' ' || _current[i] == '\t' || _current[i] == '\r' || _current[i] == '\n')) {
		i++;
	}
	if (position + i < length && _current[i] == '{') {
		brace_in_scan = true;
		return scan();
	}
	brace_queue.push_back(arrow);
	return _brace_make_synthetic(Token::COLON, arrow.start_line, arrow.start_column);
}

bool GDScriptTokenizerText::_brace_skip_comment(bool p_is_bol) {
	if (_peek() != '/') {
		return false;
	}

	if (_peek(1) == '/') {
		// Line comment. Stored with the `#` prefix the rest of the module expects, so `///` reads as `##`.
#ifdef TOOLS_ENABLED
		const int comment_line = line;
		String comment;
		while (_peek() != '\n' && !_is_at_end()) {
			comment += _advance();
		}
		if (comment.begins_with("///")) {
			comment = "##" + comment.substr(3);
		} else {
			comment = "#" + comment.substr(2);
		}
		comments[comment_line] = CommentData(comment, p_is_bol);
#else
		while (_peek() != '\n' && !_is_at_end()) {
			_advance();
		}
#endif // TOOLS_ENABLED
		return true;
	}

	if (_peek(1) != '*') {
		return false; // Division.
	}

	// Block comment. A line break inside it ends the statement it follows, as a line break would.
	// `/** */` is a documentation comment (but `/**/` is empty): each of its lines is stored as a `##` line, without
	// the `*` that conventionally starts it, so documentation reads it like `##` comments.
	const int open_line = line;
	const int open_column = column;
	const bool doc = _peek(2) == '*' && _peek(3) != '/';
	_advance();
	_advance();
	if (doc) {
		_advance();
	}
	bool make_newline = !p_is_bol;
#ifdef TOOLS_ENABLED
	String comment;
	bool comment_new_line = p_is_bol;
	Vector<int> doc_lines; // Line numbers the comment covers.
	Vector<String> doc_texts; // Their text, without decoration; empty first and last lines dropped at the end.
	auto store = [&](const String &p_text) {
		if (!doc) {
			comments[line] = CommentData("#" + p_text, comment_new_line);
			return;
		}
		String text = p_text.strip_edges(true, false);
		if (text.begins_with("*")) {
			text = text.substr(1);
		}
		doc_lines.push_back(line);
		doc_texts.push_back(text.begins_with(" ") ? text.substr(1) : text.strip_edges(false, true));
	};
#endif // TOOLS_ENABLED
	for (;;) {
		if (_is_at_end()) {
			Token error = make_error(vformat(R"(Missing "*/" to close the comment opened on line %d.)", open_line));
			error.start_line = open_line;
			error.end_line = open_line;
			error.start_column = open_column;
			error.end_column = open_column + 2;
			push_error(error);
			return true;
		}
		if (_peek() == '*' && _peek(1) == '/') {
			_advance();
			_advance();
			break;
		}
		if (_peek() == '\n') {
#ifdef TOOLS_ENABLED
			store(comment);
			comment = String();
			comment_new_line = true;
#endif // TOOLS_ENABLED
			_advance();
			newline(make_newline);
			make_newline = false;
			continue;
		}
#ifdef TOOLS_ENABLED
		comment += _advance();
#else
		_advance();
#endif // TOOLS_ENABLED
	}
#ifdef TOOLS_ENABLED
	store(comment);
	if (doc) {
		// The text goes on the comment's last lines, so the documentation ends right above what it documents and
		// carries no empty `/**` or `*/` line.
		int first = 0;
		int last = doc_texts.size() - 1;
		while (first < last && doc_texts[first].strip_edges().is_empty()) {
			first++;
		}
		while (last > first && doc_texts[last].strip_edges().is_empty()) {
			last--;
		}
		const int count = last - first + 1;
		for (int i = 0; i < count; i++) {
			const int index = doc_lines.size() - count + i;
			comments[doc_lines[index]] = CommentData("##" + doc_texts[first + i], p_is_bol || index > 0);
		}
	}
#endif // TOOLS_ENABLED
	return true;
}

void GDScriptTokenizerText::_brace_skip_line_start() {
	// Replaces indentation tracking: leading whitespace, blank lines and comment-only lines are skipped.
	for (;;) {
		switch (_peek()) {
			case ' ':
			case '\t':
				_advance();
				break;
			case '\r':
				_advance();
				if (_peek() != '\n') {
					push_error("Stray carriage return character in source code.");
					return;
				}
				break;
			case '\n':
				_advance();
				newline(false);
				break;
			case '#': {
#ifdef TOOLS_ENABLED
				const int comment_line = line;
				String comment;
				while (_peek() != '\n' && !_is_at_end()) {
					comment += _advance();
				}
				comments[comment_line] = CommentData(comment, true);
#else
				while (_peek() != '\n' && !_is_at_end()) {
					_advance();
				}
#endif // TOOLS_ENABLED
			} break;
			case '/':
				if (!_brace_skip_comment(true)) {
					return;
				}
				break;
			default:
				return;
		}
		if (_is_at_end()) {
			return;
		}
	}
}

#endif // GDSCRIPT_BRACES_ENABLED

bool GDScriptTokenizer::is_braced_path(const String &p_path) {
#ifdef GDSCRIPT_BRACES_ENABLED
	return p_path.get_extension().nocasecmp_to("gdb") == 0;
#else
	return false;
#endif // GDSCRIPT_BRACES_ENABLED
}
