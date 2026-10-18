/**************************************************************************/
/*  gdscript_formatter.cpp                                                */
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

#include "gdscript_formatter.h"

#include "gdscript.h"
#include "gdscript_analyzer.h"
#include "gdscript_parser.h"
#include "gdscript_tokenizer.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/logger.h"
#include "core/os/os.h"
#include "core/string/char_utils.h"
#include "core/templates/hash_map.h"

using Token = GDScriptTokenizer::Token;

namespace {

enum PieceKind {
	PIECE_TOKEN,
	PIECE_LINE_COMMENT,
	PIECE_BLOCK_COMMENT,
	PIECE_CONTINUATION, // A `\` that continues the line.
};

enum BracketKind {
	BRACKET_NONE,
	BRACKET_PAREN,
	BRACKET_SQUARE,
	BRACKET_CURLY, // Dictionary or enum.
	BRACKET_BLOCK, // `.gdb` block.
};

struct Piece {
	PieceKind kind = PIECE_TOKEN;
	Token::Type type = Token::EMPTY;
	int start = 0; // Offsets into the source.
	int end = 0;
	int line = 0; // 0-based.
	int end_line = 0;
	String text;
	BracketKind opens = BRACKET_NONE;
	BracketKind closes = BRACKET_NONE;
	bool unary = false;
	bool node_path = false; // Inside a `$Node/Path` or `%Unique` run, where spacing is kept as written.
	bool walrus = false; // The `:` of `:=`.
	bool ternary_colon = false; // The `:` of `c ? a : b`.
	bool multiline_block = false; // A `.gdb` block brace whose partner is on another line.
	bool lambda_block = false; // A `.gdb` block brace of a lambda's body.
	bool synthetic = false; // Zero-width: a token the `.gdb` tokenizer adds for an arrow lambda.
	bool holds_block = false; // A `.gdb` block brace whose block holds another (non-lambda) block.
	int partner = -1; // A bracket's other half.
	int enclosing = -1; // The opening bracket of the innermost bracket around the piece.
	bool list_group = false; // An opening bracket of a list: arguments, parameters, array, dictionary or enum entries.
	bool literal_group = false; // An opening bracket of an array, dictionary or enum.
};

struct StackEntry {
	BracketKind kind = BRACKET_NONE;
	int opener_line = 0;
	int level = 0; // Indentation level of the output line the opener is on.
	int statement_level = 0; // A lambda body: the level of the statement holding the lambda.
	bool lambda_block = false;
	bool class_body = false;
};

bool _scan(const String &p_source, bool p_braced, Vector<Token> &r_tokens, String &r_error, int &r_error_line) {
	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);
#ifdef GDSCRIPT_BRACES_ENABLED
	tokenizer.set_brace_mode(p_braced);
#endif // GDSCRIPT_BRACES_ENABLED

	// The parser switches the tokenizer's multiline mode as it enters and leaves brackets, which decides where line breaks
	// count. Do the same from the brackets alone, so the stream matches what the parser would see.
	List<bool> multiline;
	for (;;) {
		const Token token = tokenizer.scan();
		if (token.type == Token::ERROR) {
			r_error = token.literal;
			r_error_line = token.start_line;
			return false;
		}
		r_tokens.push_back(token);

		bool changed = false;
		switch (token.type) {
			case Token::TK_EOF:
				return true;
			case Token::PARENTHESIS_OPEN:
			case Token::BRACKET_OPEN:
			case Token::BRACE_OPEN:
				multiline.push_back(true);
				changed = true;
				break;
			case Token::PARENTHESIS_CLOSE:
			case Token::BRACKET_CLOSE:
			case Token::BRACE_CLOSE:
				if (!multiline.is_empty()) {
					multiline.pop_back();
				}
				changed = true;
				break;
			case Token::COLON:
				if (p_braced && token.source == "{") {
					multiline.push_back(false); // A block's body has significant line breaks, even inside brackets.
					changed = true;
				}
				break;
			case Token::DEDENT:
				if (p_braced && !multiline.is_empty()) {
					multiline.pop_back(); // In brace mode every DEDENT is a block's "}".
					changed = true;
				}
				break;
			default:
				break;
		}
		if (changed) {
			tokenizer.set_multiline_mode(!multiline.is_empty() && multiline.back()->get());
		}
	}
}

Token::Type _canonical_type(Token::Type p_type) {
	switch (p_type) {
		case Token::AMPERSAND_AMPERSAND:
			return Token::AND;
		case Token::PIPE_PIPE:
			return Token::OR;
		case Token::BANG:
			return Token::NOT;
		default:
			return p_type;
	}
}

// The tokens without what the formatter may add or remove without changing the meaning: commas right before a closing
// bracket (trailing commas), and line breaks right before the end of the file (a final newline).
Vector<Token> _without_trailing_commas(const Vector<Token> &p_tokens) {
	Vector<Token> result;
	for (int i = 0; i < p_tokens.size(); i++) {
		if (p_tokens[i].type == Token::COMMA && i + 1 < p_tokens.size()) {
			const Token::Type next = p_tokens[i + 1].type;
			if (next == Token::PARENTHESIS_CLOSE || next == Token::BRACKET_CLOSE || next == Token::BRACE_CLOSE) {
				continue;
			}
		}
		if (p_tokens[i].type == Token::NEWLINE) {
			int next = i + 1;
			while (next < p_tokens.size() && p_tokens[next].type == Token::NEWLINE) {
				next++;
			}
			if (next < p_tokens.size() && p_tokens[next].type == Token::TK_EOF) {
				continue;
			}
		}
		result.push_back(p_tokens[i]);
	}
	return result;
}

// Index into `p_a` of the first token that differs, or -1 when the streams are equivalent. Line breaks before the first
// token are ignored, as the parser ignores them; so are trailing commas. `p_a` and `p_b` must already be without them.
int _first_difference(const Vector<Token> &p_a, const Vector<Token> &p_b) {
	int skip_a = 0;
	while (skip_a < p_a.size() && p_a[skip_a].type == Token::NEWLINE) {
		skip_a++;
	}
	int skip_b = 0;
	while (skip_b < p_b.size() && p_b[skip_b].type == Token::NEWLINE) {
		skip_b++;
	}
	const int count = MIN(p_a.size() - skip_a, p_b.size() - skip_b);
	for (int i = 0; i < count; i++) {
		const Token &a = p_a[skip_a + i];
		const Token &b = p_b[skip_b + i];
		if (_canonical_type(a.type) != _canonical_type(b.type)) {
			return i;
		}
		if ((a.type == Token::IDENTIFIER || a.type == Token::ANNOTATION || a.type == Token::LITERAL) && a.literal != b.literal) {
			return skip_a + i;
		}
	}
	return p_a.size() - skip_a == p_b.size() - skip_b ? -1 : skip_a + count;
}

int _line_of(const Vector<int> &p_line_starts, int p_offset) {
	int low = 0;
	int high = p_line_starts.size() - 1;
	while (low < high) {
		const int mid = (low + high + 1) / 2;
		if (p_line_starts[mid] <= p_offset) {
			low = mid;
		} else {
			high = mid - 1;
		}
	}
	return low;
}

// Comments and line continuations between two tokens. Anything else there means the tokens did not cover the source,
// which the formatter refuses to guess about.
bool _scan_gap(const String &p_source, int p_from, int p_to, bool p_braced, const Vector<int> &p_line_starts, Vector<Piece> &r_pieces) {
	int i = p_from;
	while (i < p_to) {
		const char32_t c = p_source[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			i++;
			continue;
		}
		Piece piece;
		piece.start = i;
		if (c == '#' || (p_braced && c == '/' && i + 1 < p_to && p_source[i + 1] == '/')) {
			int end = i;
			while (end < p_to && p_source[end] != '\n' && p_source[end] != '\r') {
				end++;
			}
			piece.kind = PIECE_LINE_COMMENT;
			piece.end = end;
		} else if (p_braced && c == '/' && i + 1 < p_to && p_source[i + 1] == '*') {
			const int close = p_source.find("*/", i + 2);
			if (close < 0 || close + 2 > p_to) {
				return false;
			}
			piece.kind = PIECE_BLOCK_COMMENT;
			piece.end = close + 2;
		} else if (c == '\\') {
			piece.kind = PIECE_CONTINUATION;
			piece.end = i + 1;
		} else if (p_braced && c == '=' && i + 1 < p_to && p_source[i + 1] == '>') {
			// The `=>` of an arrow lambda with a block body: the tokenizer gives it no token, the block is the body.
			piece.kind = PIECE_TOKEN;
			piece.type = Token::EMPTY;
			piece.end = i + 2;
		} else {
			return false;
		}
		piece.text = p_source.substr(piece.start, piece.end - piece.start);
		piece.line = _line_of(p_line_starts, piece.start);
		piece.end_line = _line_of(p_line_starts, piece.end - 1);
		r_pieces.push_back(piece);
		i = piece.end;
	}
	return true;
}

bool _is_binary_operator(Token::Type p_type) {
	switch (p_type) {
		case Token::EQUAL:
		case Token::PLUS_EQUAL:
		case Token::MINUS_EQUAL:
		case Token::STAR_EQUAL:
		case Token::STAR_STAR_EQUAL:
		case Token::SLASH_EQUAL:
		case Token::PERCENT_EQUAL:
		case Token::LESS_LESS_EQUAL:
		case Token::GREATER_GREATER_EQUAL:
		case Token::AMPERSAND_EQUAL:
		case Token::PIPE_EQUAL:
		case Token::CARET_EQUAL:
		case Token::EQUAL_EQUAL:
		case Token::BANG_EQUAL:
		case Token::LESS:
		case Token::LESS_EQUAL:
		case Token::GREATER:
		case Token::GREATER_EQUAL:
		case Token::PLUS:
		case Token::MINUS:
		case Token::STAR:
		case Token::STAR_STAR:
		case Token::SLASH:
		case Token::PERCENT:
		case Token::AMPERSAND:
		case Token::PIPE:
		case Token::CARET:
		case Token::LESS_LESS:
		case Token::GREATER_GREATER:
		case Token::AMPERSAND_AMPERSAND:
		case Token::PIPE_PIPE:
		case Token::AND:
		case Token::OR:
		case Token::NOT: // `not in`; a prefix `not` is unary.
		case Token::TK_IN:
		case Token::IS:
		case Token::AS:
		case Token::WHEN:
		case Token::FORWARD_ARROW:
		case Token::QUESTION_MARK: // `.gdb`: `c ? a : b`
			return true;
		default:
			return false;
	}
}

bool _calls_with_paren(const Piece &p_piece, const Piece *p_before) {
	switch (p_piece.type) {
		case Token::IDENTIFIER:
		case Token::PARENTHESIS_CLOSE:
		case Token::BRACKET_CLOSE:
		case Token::FUNC:
		case Token::SUPER:
		case Token::PRELOAD:
		case Token::ASSERT:
		case Token::ANNOTATION:
		case Token::CONST_PI:
		case Token::CONST_TAU:
		case Token::CONST_INF:
		case Token::CONST_NAN:
			return true;
		case Token::MATCH:
		case Token::WHEN:
			// `String.match()`, but `match (x) {`.
			return p_before != nullptr && p_before->kind == PIECE_TOKEN && p_before->type == Token::PERIOD;
		default:
			return false;
	}
}

String _spacing_rule(const Piece &p_a, const Piece &p_b, const Piece *p_before_a, const String &p_source, const GDScriptFormatter::Options &p_options, bool p_to_indented);

// The spacing rule, except that two pieces never touch where they would read as one word (`else` and a `!` respelled
// `not`, or `4` and `not`).
String _spacing(const Piece &p_a, const Piece &p_b, const Piece *p_before_a, const String &p_source, const GDScriptFormatter::Options &p_options, bool p_to_indented) {
	const String spacing = _spacing_rule(p_a, p_b, p_before_a, p_source, p_options, p_to_indented);
	if (spacing.is_empty() && !p_a.text.is_empty() && !p_b.text.is_empty() && is_unicode_identifier_continue(p_a.text[p_a.text.length() - 1]) && is_unicode_identifier_continue(p_b.text[0])) {
		return " ";
	}
	return spacing;
}

String _spacing_rule(const Piece &p_a, const Piece &p_b, const Piece *p_before_a, const String &p_source, const GDScriptFormatter::Options &p_options, bool p_to_indented) {
	const String original = p_source.substr(p_a.end, p_b.start - p_a.end);
	const String fallback = original.is_empty() ? String() : String(" ");

	if (p_a.node_path && p_b.node_path) {
		return original;
	}
	if (p_a.text == "=>" || p_b.text == "=>") {
		return " "; // `.gdb` arrow lambda.
	}
	if (p_b.kind != PIECE_TOKEN || p_a.kind != PIECE_TOKEN) {
		if (p_b.kind == PIECE_LINE_COMMENT || p_b.kind == PIECE_CONTINUATION) {
			return " ";
		}
		return fallback; // Inline block comments keep their side's spacing, normalized to one space.
	}

	const Token::Type a = p_a.type;
	const Token::Type b = p_b.type;

	if (p_b.opens == BRACKET_BLOCK) {
		return p_to_indented ? String() : String(" "); // `if x {`, or `if x:` when converting to `.gd`.
	}
	if (p_b.closes == BRACKET_BLOCK) {
		return (p_a.opens == BRACKET_BLOCK && p_b.text != "pass") ? String() : String(" ");
	}
	if (p_a.opens == BRACKET_BLOCK) {
		return " ";
	}
	if (p_a.closes == BRACKET_BLOCK) {
		if (b == Token::BRACE_CLOSE) {
			return p_options.spaces_in_braces ? String(" ") : String(); // A lambda ending a dictionary.
		}
		if (b == Token::COMMA || b == Token::SEMICOLON || b == Token::PERIOD || b == Token::PARENTHESIS_CLOSE || b == Token::BRACKET_CLOSE) {
			return String();
		}
		return " ";
	}

	if (b == Token::COMMA || b == Token::SEMICOLON || b == Token::PARENTHESIS_CLOSE || b == Token::BRACKET_CLOSE) {
		return String();
	}
	if (b == Token::BRACE_CLOSE) {
		return (a == Token::BRACE_OPEN || !p_options.spaces_in_braces) ? String() : String(" ");
	}
	if (a == Token::PARENTHESIS_OPEN || a == Token::BRACKET_OPEN) {
		return String();
	}
	if (a == Token::BRACE_OPEN) {
		return p_options.spaces_in_braces ? String(" ") : String();
	}
	if (a == Token::COMMA || a == Token::SEMICOLON) {
		return " ";
	}
	if (a == Token::PERIOD || b == Token::PERIOD) {
		return String();
	}
	if (b == Token::COLON) {
		return (p_b.walrus || p_b.ternary_colon) ? String(" ") : String();
	}
	if (a == Token::COLON) {
		return b == Token::EQUAL ? String() : String(" "); // `:=`
	}
	if (a == Token::PERIOD_PERIOD_PERIOD || a == Token::DOLLAR) {
		return String();
	}
	if (p_a.unary) {
		if ((a == Token::MINUS || a == Token::PLUS) && b == Token::LITERAL && !p_b.text.is_empty() && (is_digit(p_b.text[0]) || p_b.text[0] == '.')) {
			return original; // `- 2` is a minus and a number; `-2` would be one number literal.
		}
		return a == Token::NOT ? String(" ") : String();
	}
	if (b == Token::PARENTHESIS_OPEN) {
		return _calls_with_paren(p_a, p_before_a) ? String() : String(" ");
	}
	if (b == Token::BRACKET_OPEN) {
		return Token(a).can_precede_bin_op() ? String() : String(" ");
	}
	if ((_is_binary_operator(b) && !p_b.unary) || _is_binary_operator(a)) {
		return " ";
	}
	return fallback;
}

String _convert_comment(const String &p_text, GDScriptFormatter::CommentStyle p_style) {
	const bool slashes = p_text.begins_with("//");
	const String marker = slashes ? (p_text.begins_with("///") ? "///" : "//") : (p_text.begins_with("##") ? "##" : "#");
	const String body = p_text.substr(marker.length());
	const bool doc = marker == "///" || marker == "##";
	if (!slashes && (body.begins_with("region") || body.begins_with("endregion"))) {
		return p_text; // Code region markers are read by the script editor as written.
	}
	if (p_style == GDScriptFormatter::COMMENT_STYLE_SLASHES && !slashes) {
		return (doc ? "///" : "//") + body;
	}
	if (p_style == GDScriptFormatter::COMMENT_STYLE_HASH && slashes) {
		return (doc ? "##" : "#") + body;
	}
	return p_text;
}

struct LineInfo {
	int first_piece = -1;
	bool member_level = false; // At the top of the file or of a class body.
	bool decl = false; // The line declares a member.
	bool decl_callable = false;
	bool comment_only = false;
	int member_blank = -1; // Blank lines this member needs before it, or -1 to keep what is there.
	int scope = 0; // The file (0) or the class body the line is in.
	int gd_depth = 0; // Block depth of a `.gd` line.
};

// An output line, and where each piece's text starts in it (x: column, y: piece index).
using OutPieces = Vector<Vector2i>;

String _lead(const String &p_line) {
	int length = 0;
	while (length < p_line.length() && (p_line[length] == ' ' || p_line[length] == '\t')) {
		length++;
	}
	return p_line.substr(0, length);
}

int _visual_width(const String &p_line, int p_tab_width) {
	int width = 0;
	for (int i = 0; i < p_line.length(); i++) {
		width += p_line[i] == '\t' ? p_tab_width : 1;
	}
	return width;
}

// Inserts `p_count` spaces before entry `p_entry` of a line.
void _pad(String &r_line, OutPieces &r_pieces, int p_entry, int p_count) {
	if (p_count <= 0) {
		return;
	}
	r_line = r_line.insert(r_pieces[p_entry].x, String(" ").repeat(p_count));
	for (int i = p_entry; i < r_pieces.size(); i++) {
		r_pieces.write[i].x += p_count;
	}
}

// Lines up one column in each run of consecutive lines with the same indentation: `p_anchors` gives, per line, the entry
// to line up or -1. Only spaces before the anchors change.
void _align_runs(Vector<String> &r_out, Vector<OutPieces> &r_pieces, const Vector<int> &p_anchors) {
	int run_start = 0;
	for (int i = 0; i <= r_out.size(); i++) {
		const bool continues = i < r_out.size() && p_anchors[i] >= 0 && i > run_start && p_anchors[run_start] >= 0 && _lead(r_out[i]) == _lead(r_out[run_start]);
		if (continues) {
			continue;
		}
		if (i - run_start >= 2 && p_anchors[run_start] >= 0) {
			int column = 0;
			for (int j = run_start; j < i; j++) {
				column = MAX(column, r_pieces[j][p_anchors[j]].x);
			}
			for (int j = run_start; j < i; j++) {
				_pad(r_out.write[j], r_pieces.write[j], p_anchors[j], column - r_pieces[j][p_anchors[j]].x);
			}
		}
		run_start = i;
	}
}

// Tier 3 alignment. Each rule finds the entry to line up on each output line.
void _align(Vector<String> &r_out, Vector<OutPieces> &r_pieces, const Vector<Piece> &p_pieces, const GDScriptFormatter::Options &p_options, const String &p_unit, bool p_braced) {
	const int count = r_out.size();
	// Bracket depth inside the line before each entry.
	auto depths = [&](const OutPieces &p_line) {
		Vector<int> result;
		int depth = 0;
		for (const Vector2i &entry : p_line) {
			const Piece &piece = p_pieces[entry.y];
			if (piece.kind == PIECE_TOKEN && piece.closes != BRACKET_NONE) {
				depth = MAX(0, depth - 1);
			}
			result.push_back(depth);
			if (piece.kind == PIECE_TOKEN && piece.opens != BRACKET_NONE) {
				depth++;
			}
		}
		return result;
	};
	auto first_token = [&](const OutPieces &p_line) {
		for (int e = 0; e < p_line.size(); e++) {
			if (p_pieces[p_line[e].y].kind == PIECE_TOKEN && !p_pieces[p_line[e].y].text.is_empty()) {
				return e;
			}
		}
		return -1;
	};

	if (p_options.align_assignments) {
		Vector<int> anchors;
		anchors.resize(count);
		anchors.fill(-1);
		for (int i = 0; i < count; i++) {
			const OutPieces &line = r_pieces[i];
			int head = first_token(line);
			while (head >= 0 && head < line.size() && (p_pieces[line[head].y].type == Token::ANNOTATION || p_pieces[line[head].y].type == Token::STATIC)) {
				head++; // `@export var`, `static var`.
			}
			if (head < 0 || head >= line.size() || (p_pieces[line[head].y].type != Token::VAR && p_pieces[line[head].y].type != Token::TK_CONST)) {
				continue;
			}
			const Vector<int> depth = depths(line);
			for (int e = head; e < line.size(); e++) {
				const Piece &piece = p_pieces[line[e].y];
				if (depth[e] != 0) {
					continue;
				}
				if (piece.opens == BRACKET_BLOCK) {
					break; // A property's accessors.
				}
				if (piece.walrus || (piece.type == Token::EQUAL && !(e > 0 && p_pieces[line[e - 1].y].walrus))) {
					anchors.write[i] = e;
					break;
				}
			}
		}
		_align_runs(r_out, r_pieces, anchors);
	}

	if (p_options.align_dictionary_values) {
		Vector<int> anchors;
		anchors.resize(count);
		anchors.fill(-1);
		for (int i = 0; i < count; i++) {
			const OutPieces &line = r_pieces[i];
			const int head = first_token(line);
			if (head < 0) {
				continue;
			}
			const int opener = p_pieces[line[head].y].enclosing;
			if (opener < 0 || p_pieces[opener].opens != BRACKET_CURLY || p_pieces[opener].partner < 0 || p_pieces[opener].line == p_pieces[p_pieces[opener].partner].line) {
				continue;
			}
			for (int e = head; e + 1 < line.size(); e++) {
				const Piece &piece = p_pieces[line[e].y];
				if (piece.enclosing != opener) {
					continue;
				}
				if (piece.type == Token::COLON && !piece.walrus && !piece.ternary_colon) {
					anchors.write[i] = e + 1; // The value.
					break;
				}
				if (piece.type == Token::EQUAL) {
					anchors.write[i] = e; // An enum value.
					break;
				}
			}
		}
		_align_runs(r_out, r_pieces, anchors);
	}

	if (p_options.align_match_branches) {
		// One-line branches: `.gdb` `pattern { body }`, `.gd` `pattern: body`. Branch lines are one level inside a line that
		// starts with `match`.
		Vector<int> anchors;
		anchors.resize(count);
		anchors.fill(-1);
		List<String> branch_leads;
		for (int i = 0; i < count; i++) {
			const OutPieces &line = r_pieces[i];
			const int head = first_token(line);
			if (head < 0) {
				continue;
			}
			const String lead = _lead(r_out[i]);
			while (!branch_leads.is_empty() && lead.length() < branch_leads.back()->get().length()) {
				branch_leads.pop_back();
			}
			if (!branch_leads.is_empty() && lead == branch_leads.back()->get()) {
				const Vector<int> depth = depths(line);
				for (int e = head; e + 1 < line.size(); e++) {
					const Piece &piece = p_pieces[line[e].y];
					if (depth[e] != 0) {
						continue;
					}
					if (p_braced && piece.opens == BRACKET_BLOCK && piece.partner >= 0 && p_pieces[piece.partner].line == piece.line) {
						anchors.write[i] = e;
						break;
					}
					if (!p_braced && piece.type == Token::COLON && !piece.walrus && !piece.ternary_colon && p_pieces[line[e + 1].y].kind == PIECE_TOKEN) {
						anchors.write[i] = e + 1;
						break;
					}
				}
			}
			if (p_pieces[line[head].y].type == Token::MATCH) {
				branch_leads.push_back(lead + p_unit);
			}
		}
		_align_runs(r_out, r_pieces, anchors);
	}

	// Last: the other rules move comments.
	if (p_options.align_comments) {
		Vector<int> anchors;
		anchors.resize(count);
		anchors.fill(-1);
		for (int i = 0; i < count; i++) {
			const OutPieces &line = r_pieces[i];
			const int head = first_token(line);
			if (head >= 0 && line.size() >= 2 && p_pieces[line[line.size() - 1].y].kind == PIECE_LINE_COMMENT) {
				anchors.write[i] = line.size() - 1;
			}
		}
		_align_runs(r_out, r_pieces, anchors);
	}
}

// Line wrapping: a line longer than the limit has its last outermost list that opens and closes on the line put one item
// per line. Returns whether any line changed; a line can need more passes (its items may still be too long), which
// `format()` runs.
bool _wrap(Vector<String> &r_out, Vector<OutPieces> &r_pieces, const Vector<Piece> &p_pieces, const GDScriptFormatter::Options &p_options, const String &p_unit, bool p_braced) {
	bool changed = false;
	Vector<String> out;
	Vector<OutPieces> out_pieces;
	for (int i = 0; i < r_out.size(); i++) {
		const String &text = r_out[i];
		const OutPieces &line = r_pieces[i];
		int group = -1; // Entry of the opening bracket to break.
		int group_close = -1;
		if (_visual_width(text, MAX(1, p_options.indent_size)) > p_options.max_line_length) {
			HashMap<int, int> entry_of; // Piece index -> entry.
			bool skip = false;
			for (int e = 0; e < line.size(); e++) {
				entry_of[line[e].y] = e;
				const PieceKind kind = p_pieces[line[e].y].kind;
				skip = skip || kind == PIECE_CONTINUATION || kind == PIECE_BLOCK_COMMENT;
			}
			int depth = 0;
			int best_depth = INT_MAX;
			for (int e = 0; e < line.size() && !skip; e++) {
				const Piece &piece = p_pieces[line[e].y];
				if (piece.kind != PIECE_TOKEN) {
					continue;
				}
				if (piece.closes != BRACKET_NONE) {
					depth = MAX(0, depth - 1);
				}
				if (piece.opens != BRACKET_NONE) {
					// In `.gd` a lambda's body is indentation-sensitive, so a list holding one keeps its layout.
					bool holds_gd_lambda = false;
					for (int inner = line[e].y + 1; !p_braced && inner < piece.partner; inner++) {
						holds_gd_lambda = holds_gd_lambda || p_pieces[inner].type == Token::FUNC;
					}
					if (piece.list_group && !holds_gd_lambda && piece.partner >= 0 && entry_of.has(piece.partner) && entry_of[piece.partner] > e + 1 && depth <= best_depth) {
						best_depth = depth;
						group = e;
						group_close = entry_of[piece.partner];
					}
					depth++;
				}
			}
		}
		if (group < 0) {
			out.push_back(text);
			out_pieces.push_back(line);
			continue;
		}

		// The items: the entries between the brackets, split at the list's own commas.
		const int opener = line[group].y;
		Vector<String> items;
		bool comment_inside = false;
		int item_start = group + 1;
		for (int e = group + 1; e <= group_close; e++) {
			const Piece &piece = p_pieces[line[e].y];
			comment_inside = comment_inside || piece.kind != PIECE_TOKEN;
			const bool separator = e == group_close || (piece.type == Token::COMMA && piece.enclosing == opener && piece.text == ",");
			const bool item_end_comma = piece.enclosing == opener && piece.text.ends_with(",") && piece.type != Token::COMMA;
			if (!separator && !item_end_comma) {
				continue;
			}
			const int last = separator ? e - 1 : e;
			if (last >= item_start) {
				const int from = line[item_start].x;
				const int to = line[last].x + p_pieces[line[last].y].text.length();
				String item = text.substr(from, to - from);
				if (item_end_comma) {
					item = item.substr(0, item.length() - 1); // A trailing comma the formatter added.
				}
				items.push_back(item.strip_edges());
			}
			item_start = e + 1;
		}
		while (!items.is_empty() && items[items.size() - 1].is_empty()) {
			items.remove_at(items.size() - 1);
		}
		if (comment_inside || items.is_empty()) {
			out.push_back(text);
			out_pieces.push_back(line);
			continue;
		}

		const Piece &open_piece = p_pieces[opener];
		const String lead = _lead(text);
		const int step = open_piece.opens == BRACKET_PAREN ? p_options.paren_continuation : p_options.bracket_continuation;
		const String item_lead = lead + p_unit.repeat(MAX(1, step));
		const bool comma = p_options.trailing_commas == GDScriptFormatter::TRAILING_COMMAS_ALL ||
				(p_options.trailing_commas == GDScriptFormatter::TRAILING_COMMAS_LITERALS && open_piece.literal_group);
		const String &last_item = items[items.size() - 1];
		const bool rest = last_item.begins_with("..");
		out.push_back(text.substr(0, line[group].x + open_piece.text.length()));
		out_pieces.push_back(OutPieces());
		for (int k = 0; k < items.size(); k++) {
			const bool final = k == items.size() - 1;
			out.push_back(item_lead + items[k] + ((!final || (comma && !rest)) ? String(",") : String()));
			out_pieces.push_back(OutPieces());
		}
		out.push_back(lead + text.substr(line[group_close].x));
		out_pieces.push_back(OutPieces());
		changed = true;
	}
	r_out = out;
	r_pieces = out_pieces;
	return changed;
}

} // namespace

GDScriptFormatter::Options GDScriptFormatter::get_preset(const String &p_name) {
	Options options;
	if (p_name == "js") {
		options.use_spaces = true;
		options.indent_size = 4;
		options.paren_continuation = 1;
		options.bracket_continuation = 1;
		options.spaces_in_braces = true;
		options.operator_style = OPERATOR_STYLE_SYMBOLS;
		options.comment_style = COMMENT_STYLE_SLASHES;
		options.elif_style = ELIF_STYLE_ELSE_IF;
		options.blank_lines_around_members = 1;
		options.max_blank_lines = 1;
		options.trailing_commas = TRAILING_COMMAS_ALL;
	} else {
		// The official GDScript style guide.
		options.use_spaces = false;
		options.indent_size = 4;
		options.paren_continuation = 2;
		options.bracket_continuation = 1;
		options.spaces_in_braces = false;
		options.operator_style = OPERATOR_STYLE_WORDS;
		options.comment_style = COMMENT_STYLE_KEEP;
		options.elif_style = ELIF_STYLE_ELIF;
		options.blank_lines_around_members = 2;
		options.max_blank_lines = 1;
		options.trailing_commas = TRAILING_COMMAS_LITERALS;
	}
	return options;
}

String GDScriptFormatter::get_default_preset(bool p_braced) {
	return p_braced ? "js" : "godot";
}

GDScriptFormatter::Result GDScriptFormatter::format(const String &p_source, const String &p_path, const Options &p_options) {
	bool wrapped = false;
	Result result = _format(p_source, p_path, p_options, false, &wrapped);
	if (result.error != OK && result.message.begins_with("Internal formatter error")) {
		// Trailing commas, alignment and wrapping are the newest rules: rather than give up on the file, format it without
		// them, and report the error only if that fails too.
		Options basic = p_options;
		basic.trailing_commas = TRAILING_COMMAS_KEEP;
		basic.max_line_length = 0;
		basic.align_assignments = basic.align_comments = basic.align_dictionary_values = basic.align_match_branches = false;
		Result fallback = _format(p_source, p_path, basic, false);
		if (fallback.error != OK) {
			return result;
		}
		fallback.note = vformat("Formatted without trailing commas, alignment and wrapping (line %d: %s)", result.line, result.message);
		return fallback;
	}
	// A wrapped line changes the layout the next pass sees (a block that now spans lines gets its own lines, an item may
	// still be too long), so repeat until the text is stable, which also keeps formatting idempotent.
	for (int pass = 0; wrapped && pass < 100 && result.error == OK; pass++) {
		wrapped = false;
		const Result next = _format(result.output, p_path, p_options, false, &wrapped);
		if (next.error != OK || next.output == result.output) {
			break;
		}
		result = next;
	}
	return result;
}

GDScriptFormatter::Result GDScriptFormatter::_format(const String &p_source, const String &p_path, const Options &p_options, bool p_to_indented, bool *r_wrapped) {
	Result result;
	const bool braced = GDScriptTokenizer::is_braced_path(p_path);
	const bool crlf = p_options.use_crlf || p_source.contains("\r\n");
	const String source = p_source.replace("\r\n", "\n");

	auto fail = [&](const String &p_message, int p_line) {
		result.error = ERR_PARSE_ERROR;
		result.message = p_message;
		result.line = p_line;
		return result;
	};

	// The file must be valid to start with; formatting broken code would be guessing.
	Vector<Token> tokens;
	String scan_error;
	int scan_error_line = 0;
	if (!_scan(source, braced, tokens, scan_error, scan_error_line)) {
		return fail(scan_error, scan_error_line);
	}
	const String parse_path = p_path.is_empty() ? String(braced ? "res://format.gdb" : "res://format.gd") : p_path;
	{
		GDScriptParser parser;
		if (parser.parse(source, parse_path, false) != OK) {
			const List<GDScriptParser::ParserError> &errors = parser.get_errors();
			if (errors.is_empty()) {
				return fail("The script has syntax errors.", 0);
			}
			return fail(errors.front()->get().message, errors.front()->get().start_line);
		}
	}

	Vector<int> line_starts;
	line_starts.push_back(0);
	for (int i = 0; i < source.length(); i++) {
		if (source[i] == '\n') {
			line_starts.push_back(i + 1);
		}
	}
	const int line_count = line_starts.size();
	// Clamped: for what ends the file without a line break, positions past the end are reported (the column after
	// it, or the start of a line that does not exist).
	auto offset_of = [&](int p_line, int p_column) {
		if (p_line > line_count) {
			return source.length();
		}
		return CLAMP(line_starts[CLAMP(p_line - 1, 0, line_count - 1)] + p_column - 1, 0, source.length());
	};

	// Pieces: every token that has text, plus the comments and continuations between them, in source order.
	Vector<Piece> pieces;
	int previous_end = 0;
	for (const Token &token : tokens) {
		Piece piece;
		piece.type = token.type;
		if (token.type == Token::NEWLINE || token.type == Token::INDENT || token.type == Token::DEDENT || token.type == Token::TK_EOF) {
			const int offset = offset_of(token.start_line, token.start_column);
			if (!(braced && token.type == Token::DEDENT && offset < source.length() && source[offset] == '}')) {
				continue;
			}
			piece.start = offset; // A block's closing "}".
			piece.end = offset + 1;
			piece.text = "}";
			piece.closes = BRACKET_BLOCK;
		} else if (token.source.is_empty()) {
			// Synthetic. An arrow lambda's `func`, its parentheses around a lone parameter and its `:` become zero-width
			// pieces, which only a conversion to `.gd` spells out; others (`pass` of an empty block) are skipped.
			const bool arrow_part = braced && (token.type == Token::FUNC || token.type == Token::PARENTHESIS_OPEN || token.type == Token::PARENTHESIS_CLOSE || token.type == Token::COLON);
			if (!arrow_part) {
				continue;
			}
			piece.start = offset_of(token.start_line, token.start_column);
			piece.end = piece.start;
			piece.synthetic = true;
			piece.text = String();
			if (token.type == Token::PARENTHESIS_OPEN) {
				piece.opens = BRACKET_PAREN;
			} else if (token.type == Token::PARENTHESIS_CLOSE) {
				piece.closes = BRACKET_PAREN;
			}
		} else {
			piece.start = offset_of(token.start_line, token.start_column);
			// A token's text is its slice of the source. Not `end_line`/`end_column`: for a token that ends the file
			// without a final line break, the tokenizer reports an end past it.
			piece.end = piece.start + token.source.length();
			piece.text = token.source;
			if (source.substr(piece.start, piece.end - piece.start) != piece.text) {
				return fail(vformat("Internal formatter error: token \"%s\" is not where the tokenizer placed it.", token.source), token.start_line);
			}
			switch (token.type) {
				case Token::PARENTHESIS_OPEN:
					piece.opens = BRACKET_PAREN;
					break;
				case Token::PARENTHESIS_CLOSE:
					piece.closes = BRACKET_PAREN;
					break;
				case Token::BRACKET_OPEN:
					piece.opens = BRACKET_SQUARE;
					break;
				case Token::BRACKET_CLOSE:
					piece.closes = BRACKET_SQUARE;
					break;
				case Token::BRACE_OPEN:
					piece.opens = BRACKET_CURLY;
					break;
				case Token::BRACE_CLOSE:
					piece.closes = BRACKET_CURLY;
					break;
				case Token::COLON:
					if (braced && token.source == "{") {
						piece.opens = BRACKET_BLOCK;
					}
					break;
				default:
					break;
			}
		}
		if (piece.start < previous_end) {
			return fail("Internal formatter error: tokens overlap.", token.start_line);
		}
		if (!_scan_gap(source, previous_end, piece.start, braced, line_starts, pieces)) {
			return fail("Internal formatter error: unrecognized text between tokens.", _line_of(line_starts, previous_end) + 1);
		}
		piece.line = _line_of(line_starts, piece.start);
		piece.end_line = _line_of(line_starts, MAX(piece.start, piece.end - 1));
		pieces.push_back(piece);
		previous_end = piece.end;
	}
	if (!_scan_gap(source, previous_end, source.length(), braced, line_starts, pieces)) {
		return fail("Internal formatter error: unrecognized text after the last token.", line_count);
	}

	// Context: unary operators, node paths, operator and comment style.
	{
		const Piece *previous_token = nullptr;
		bool in_node_path = false;
		for (int i = 0; i < pieces.size(); i++) {
			Piece &piece = pieces.write[i];
			if (piece.kind != PIECE_TOKEN) {
				if (piece.kind == PIECE_LINE_COMMENT && braced && (p_to_indented || p_options.comment_style != COMMENT_STYLE_KEEP)) {
					piece.text = _convert_comment(piece.text, p_to_indented ? COMMENT_STYLE_HASH : p_options.comment_style);
				} else if (piece.kind == PIECE_BLOCK_COMMENT && p_to_indented) {
					// `.gd` has only line comments: one `#` line per line of the block comment. A `/** */`
					// documentation comment becomes `##` lines, without its `/**` and `*/` lines or the `*` that
					// starts each line.
					const bool doc = piece.text.begins_with("/**") && piece.text != "/**/";
					const String body = piece.text.substr(doc ? 3 : 2, piece.text.length() - (doc ? 5 : 4));
					const Vector<String> body_lines = body.split("\n");
					Vector<String> comment_lines;
					for (int line_index = 0; line_index < body_lines.size(); line_index++) {
						String text = body_lines[line_index].strip_edges(false, true);
						if (doc) {
							text = text.strip_edges(true, false);
							text = text.begins_with("*") ? text.substr(1) : text;
							text = text.begins_with(" ") ? text.substr(1) : text;
							if (text.is_empty() && (line_index == 0 || line_index == body_lines.size() - 1) && body_lines.size() > 1) {
								continue;
							}
							comment_lines.push_back("## " + text);
						} else {
							comment_lines.push_back("#" + text);
						}
					}
					piece.text = String("\n").join(comment_lines).strip_edges(false, true);
				}
				in_node_path = false;
				continue;
			}
			const bool after_value = previous_token != nullptr && previous_token->closes != BRACKET_BLOCK && Token(previous_token->type).can_precede_bin_op();
			switch (piece.type) {
				case Token::MINUS:
				case Token::PLUS:
				case Token::PERCENT:
				case Token::NOT:
					piece.unary = !after_value;
					break;
				case Token::BANG:
				case Token::TILDE:
					piece.unary = true;
					break;
				default:
					break;
			}

			if (in_node_path && previous_token != nullptr && previous_token->end == piece.start && (piece.type == Token::IDENTIFIER || piece.type == Token::SLASH || piece.type == Token::PERCENT || piece.type == Token::LITERAL || Token(piece.type).is_node_name())) {
				piece.node_path = true;
			} else {
				in_node_path = piece.type == Token::DOLLAR || (piece.type == Token::PERCENT && piece.unary);
				piece.node_path = in_node_path;
			}

			if (p_to_indented) {
				// An arrow lambda in `.gd`: `(x) => x * 2` is `func(x): return x * 2`, `(x) => {` is `func(x):`.
				if (piece.synthetic) {
					piece.text = piece.type == Token::FUNC ? "func" : (piece.type == Token::PARENTHESIS_OPEN ? "(" : (piece.type == Token::PARENTHESIS_CLOSE ? ")" : ":"));
				} else if (piece.text == "=>") {
					piece.text = piece.type == Token::RETURN ? "return" : "";
				}
			}

			// After a `.`, a keyword is an attribute name (`dict.elif`, `dict.and`): never respell it.
			const bool attribute = previous_token != nullptr && previous_token->type == Token::PERIOD;

			if (braced && piece.type == Token::ELIF && !attribute) {
				// `.gdb` accepts `elif` and `else if` alike.
				if (p_to_indented || p_options.elif_style == ELIF_STYLE_ELIF) {
					piece.text = "elif";
				} else if (p_options.elif_style == ELIF_STYLE_ELSE_IF || piece.text.begins_with("else")) {
					piece.text = "else if";
				}
			}

			const bool after_is = previous_token != nullptr && previous_token->type == Token::IS; // `is not`
			if (attribute) {
				// Leave it as written.
			} else if (p_options.operator_style == OPERATOR_STYLE_WORDS) {
				if (piece.type == Token::AMPERSAND_AMPERSAND) {
					piece.type = Token::AND;
					piece.text = "and";
				} else if (piece.type == Token::PIPE_PIPE) {
					piece.type = Token::OR;
					piece.text = "or";
				} else if (piece.type == Token::BANG && !after_value) {
					// Only a prefix `!`: after a value the parser reads `!` and `not` differently (`4!2` parses, `4 not 2`
					// does not).
					piece.type = Token::NOT;
					piece.text = "not";
				}
			} else if (p_options.operator_style == OPERATOR_STYLE_SYMBOLS) {
				if (piece.type == Token::AND) {
					piece.type = Token::AMPERSAND_AMPERSAND;
					piece.text = "&&";
				} else if (piece.type == Token::OR) {
					piece.type = Token::PIPE_PIPE;
					piece.text = "||";
				} else if (piece.type == Token::NOT && piece.unary && !after_is) {
					piece.type = Token::BANG;
					piece.text = "!";
				}
			}
			previous_token = &pieces[i];
		}
	}

	// `:=`, which block braces span lines, which belong to lambdas, and what they become in `.gd`.
	{
		List<int> openers;
		List<int> question_depths; // Bracket depth of each `?` still waiting for its `:`.
		List<int> lambda_depths; // Bracket depth of each `func` that starts a lambda, until its body opens.
		int depth = 0;
		int previous_token = -1;
		for (int i = 0; i < pieces.size(); i++) {
			Piece &piece = pieces.write[i];
			if (piece.kind != PIECE_TOKEN) {
				continue;
			}
			if (piece.type == Token::COLON && i + 1 < pieces.size() && pieces[i + 1].kind == PIECE_TOKEN && pieces[i + 1].type == Token::EQUAL) {
				piece.walrus = true;
			}
			if (piece.type == Token::QUESTION_MARK) {
				question_depths.push_back(depth);
			} else if (piece.type == Token::COLON && piece.opens == BRACKET_NONE && !question_depths.is_empty() && question_depths.back()->get() == depth) {
				piece.ternary_colon = true;
				question_depths.pop_back();
			}
			if (piece.type == Token::FUNC && previous_token >= 0) {
				// A `func` after `=`, `(`, `,`, `return` and the like is a lambda; one starting a statement is a method.
				const Piece &before = pieces[previous_token];
				if (before.opens != BRACKET_BLOCK && before.closes != BRACKET_BLOCK && (before.type == Token::EQUAL || before.type == Token::PARENTHESIS_OPEN || before.type == Token::BRACKET_OPEN || before.type == Token::BRACE_OPEN || before.type == Token::COMMA || before.type == Token::COLON || before.type == Token::RETURN || before.type == Token::ELSE || _is_binary_operator(before.type))) {
					lambda_depths.push_back(depth);
				}
			}
			if (piece.opens == BRACKET_BLOCK) {
				if (!lambda_depths.is_empty() && lambda_depths.back()->get() == depth) {
					piece.lambda_block = true;
					lambda_depths.pop_back();
				}
				if (!piece.lambda_block) {
					for (const int enclosing : openers) {
						pieces.write[enclosing].holds_block = true;
					}
				}
				if (p_to_indented) {
					piece.text = ":";
				}
				openers.push_back(i);
			} else if (piece.closes == BRACKET_BLOCK && !openers.is_empty()) {
				const int opener = openers.back()->get();
				openers.pop_back();
				piece.lambda_block = pieces[opener].lambda_block;
				piece.holds_block = pieces[opener].holds_block;
				if (pieces[opener].line != piece.line || pieces[opener].multiline_block) {
					pieces.write[opener].multiline_block = true;
					piece.multiline_block = true;
				}
				// A statement after this `}` on the same line gets its own line, so the blocks around it span lines.
				int next = i + 1;
				while (next < pieces.size() && pieces[next].kind != PIECE_TOKEN) {
					next++;
				}
				if (braced && !piece.lambda_block && next < pieces.size() && pieces[next].line == piece.line) {
					const Token::Type after = pieces[next].type;
					const bool joins = after == Token::ELSE || after == Token::ELIF || after == Token::COMMA || after == Token::SEMICOLON || after == Token::PERIOD || pieces[next].closes != BRACKET_NONE;
					if (!joins) {
						for (const int enclosing : openers) {
							pieces.write[enclosing].multiline_block = true;
						}
					}
				}
				if (p_to_indented) {
					piece.text = previous_token == opener ? "pass" : ""; // `{}` is an empty block; `}` has no `.gd` form.
				}
			}
			if (piece.opens != BRACKET_NONE) {
				depth++;
			} else if (piece.closes != BRACKET_NONE) {
				depth = MAX(0, depth - 1);
			}
			previous_token = i;
		}
	}

	// Bracket pairs: each half's partner, the bracket around each piece, and which brackets hold lists of items.
	{
		List<int> open;
		int previous_token = -1;
		int before_previous = -1;
		for (int i = 0; i < pieces.size(); i++) {
			Piece &piece = pieces.write[i];
			piece.enclosing = open.is_empty() ? -1 : open.back()->get();
			if (piece.kind != PIECE_TOKEN) {
				continue;
			}
			if (piece.closes != BRACKET_NONE && !open.is_empty()) {
				const int opener = open.back()->get();
				open.pop_back();
				pieces.write[opener].partner = i;
				piece.partner = opener;
				piece.enclosing = open.is_empty() ? -1 : open.back()->get();
			}
			if (piece.opens != BRACKET_NONE) {
				const Piece *before = previous_token >= 0 ? &pieces[previous_token] : nullptr;
				if (piece.synthetic) {
					// An arrow lambda's added parentheses: not written in `.gdb`.
				} else if (piece.opens == BRACKET_PAREN) {
					// A call, a parameter list (`func f(`, `signal s(`) or annotation arguments; not `(a + b)`.
					piece.list_group = before != nullptr && _calls_with_paren(*before, before_previous >= 0 ? &pieces[before_previous] : nullptr);
				} else if (piece.opens == BRACKET_SQUARE) {
					// An array, not a subscript (`a[0]`) or a type argument (`Array[int]`).
					piece.list_group = before == nullptr || before->closes == BRACKET_BLOCK || !Token(before->type).can_precede_bin_op();
					piece.literal_group = piece.list_group;
				} else if (piece.opens == BRACKET_CURLY) {
					piece.list_group = true;
					piece.literal_group = true;
				}
				open.push_back(i);
			}
			before_previous = previous_token;
			previous_token = i;
		}
	}

	// Trailing commas: a list whose closing bracket starts its own line gets a comma after its last item.
	if (!p_to_indented && p_options.trailing_commas != TRAILING_COMMAS_KEEP) {
		for (int i = 0; i < pieces.size(); i++) {
			const Piece &closer = pieces[i];
			if (closer.kind != PIECE_TOKEN || closer.partner < 0 || closer.closes == BRACKET_NONE || closer.closes == BRACKET_BLOCK) {
				continue;
			}
			const Piece &opener = pieces[closer.partner];
			if (!opener.list_group || (!opener.literal_group && p_options.trailing_commas != TRAILING_COMMAS_ALL)) {
				continue;
			}
			int last = i - 1;
			bool rest = false; // `..` (a pattern's rest) and `...args` must stay last, without a comma.
			while (last > closer.partner && pieces[last].kind != PIECE_TOKEN) {
				last--;
			}
			for (int j = closer.partner + 1; j < i; j++) {
				if (pieces[j].enclosing == closer.partner && (pieces[j].type == Token::PERIOD_PERIOD || pieces[j].type == Token::PERIOD_PERIOD_PERIOD)) {
					rest = true;
				}
			}
			Piece &item_end = pieces.write[last];
			if (rest || last == closer.partner || item_end.type == Token::COMMA || item_end.text.is_empty() || item_end.end_line >= closer.line) {
				continue;
			}
			item_end.text += ",";
		}
	}

	// Line structure: which pieces start each line, the member layout, and `.gd` block depth.
	Vector<LineInfo> lines;
	lines.resize(line_count);
	for (int i = pieces.size() - 1; i >= 0; i--) {
		lines.write[pieces[i].line].first_piece = i;
	}
	// Lines inside a multi-line piece (a block comment, a long string) belong to the line it starts on.
	Vector<int> covered_by;
	covered_by.resize(line_count);
	covered_by.fill(-1);
	for (const Piece &piece : pieces) {
		for (int covered = piece.line + 1; covered <= piece.end_line && covered < line_count; covered++) {
			covered_by.write[covered] = piece.line;
		}
	}
	{
		List<StackEntry> stack; // `.gdb` brackets and blocks.
		int bracket_depth = 0; // `.gd` brackets.
		List<bool> gd_blocks; // `.gd` open blocks: whether each is a class body.
		List<int> scopes; // Member scopes: the file, then each class body.
		scopes.push_back(0);
		int next_scope = 1;
		Token::Type last_head = Token::EMPTY;
		int token_index = 0;
		for (int line = 0; line < line_count; line++) {
			LineInfo &info = lines.write[line];
			if (info.first_piece < 0) {
				continue;
			}
			const Piece &first = pieces[info.first_piece];

			if (braced) {
				info.member_level = stack.is_empty() || (stack.back()->get().kind == BRACKET_BLOCK && stack.back()->get().class_body);
			} else {
				// The INDENT and DEDENT tokens before this line's first token.
				while (token_index < tokens.size()) {
					const Token &token = tokens[token_index];
					if (token.type == Token::TK_EOF || offset_of(token.start_line, token.start_column) > first.start) {
						break;
					}
					if (token.type == Token::INDENT) {
						const bool class_body = last_head == Token::CLASS;
						gd_blocks.push_back(class_body);
						if (class_body) {
							scopes.push_back(next_scope++);
						}
					} else if (token.type == Token::DEDENT && !gd_blocks.is_empty()) {
						if (gd_blocks.back()->get()) {
							scopes.pop_back();
						}
						gd_blocks.pop_back();
					}
					token_index++;
				}
				info.gd_depth = gd_blocks.size();
				info.member_level = bracket_depth == 0 && (gd_blocks.is_empty() || gd_blocks.back()->get());
			}
			if (first.kind == PIECE_TOKEN && first.closes != BRACKET_NONE) {
				info.member_level = false;
			}
			info.scope = scopes.back()->get();

			// What the line declares.
			info.comment_only = true;
			bool annotations_only = true;
			Token::Type head = Token::EMPTY;
			for (int i = info.first_piece; i < pieces.size() && pieces[i].line == line; i++) {
				const Piece &piece = pieces[i];
				if (piece.kind != PIECE_TOKEN) {
					continue;
				}
				info.comment_only = false;
				if (head == Token::EMPTY) {
					head = piece.type;
				}
				if (piece.type != Token::ANNOTATION) {
					annotations_only = false;
				}
				if (piece.type == Token::FUNC || piece.type == Token::CLASS) {
					info.decl_callable = true;
				}
			}
			info.decl = !info.comment_only && !annotations_only;
			if (head != Token::FUNC && head != Token::CLASS && head != Token::STATIC && head != Token::ANNOTATION) {
				info.decl_callable = false; // A lambda in an initializer, for example.
			}
			if (!info.comment_only) {
				last_head = head;
			}

			// Brackets and blocks opened and closed on this line, and on the lines a multi-line piece runs into.
			int last_line = line;
			Token::Type statement_head = Token::EMPTY;
			for (int i = info.first_piece; i < pieces.size() && pieces[i].line <= last_line; i++) {
				const Piece &piece = pieces[i];
				last_line = MAX(last_line, piece.end_line);
				if (piece.kind != PIECE_TOKEN) {
					continue;
				}
				if (statement_head == Token::EMPTY) {
					statement_head = piece.type;
				}
				if (piece.closes != BRACKET_NONE) {
					if (braced && !stack.is_empty()) {
						if (stack.back()->get().class_body) {
							scopes.pop_back();
						}
						stack.pop_back();
					} else if (!braced) {
						bracket_depth = MAX(0, bracket_depth - 1);
					}
					statement_head = Token::EMPTY;
				}
				if (piece.opens != BRACKET_NONE) {
					if (braced) {
						StackEntry entry;
						entry.kind = piece.opens;
						entry.opener_line = line;
						entry.class_body = piece.opens == BRACKET_BLOCK && statement_head == Token::CLASS;
						if (entry.class_body) {
							scopes.push_back(next_scope++);
						}
						stack.push_back(entry);
					} else {
						bracket_depth++;
					}
					if (piece.opens == BRACKET_BLOCK) {
						statement_head = Token::EMPTY;
					}
				}
				if (piece.type == Token::SEMICOLON) {
					statement_head = Token::EMPTY;
				}
			}
			line = last_line;
		}

		// `.gd` comment lines take their block from the code below them, unless they are indented deeper than it, which
		// keeps them in the block above. The tokens before a comment cannot tell, because DEDENT comes after it.
		if (!braced) {
			for (int line = line_count - 1, next_code = -1; line >= 0; line--) {
				LineInfo &info = lines.write[line];
				if (info.first_piece < 0) {
					continue;
				}
				if (!info.comment_only) {
					next_code = line;
					continue;
				}
				if (next_code < 0) {
					continue;
				}
				const int comment_lead = pieces[info.first_piece].start - line_starts[line];
				const int code_lead = pieces[lines[next_code].first_piece].start - line_starts[next_code];
				if (comment_lead <= code_lead) {
					info.gd_depth = lines[next_code].gd_depth;
					info.member_level = lines[next_code].member_level;
					info.scope = lines[next_code].scope;
				} else {
					info.member_level = false;
				}
			}
		}

		// Comments and annotations directly above a member belong to it; a function or class gets blank lines around it.
		HashMap<int, bool> last_member_callable; // Per scope.
		for (int line = 0; line < line_count; line++) {
			LineInfo &info = lines.write[line];
			if (info.first_piece < 0 || !info.member_level) {
				continue;
			}
			int previous = line - 1;
			if (previous >= 0 && lines[previous].first_piece < 0 && covered_by[previous] >= 0) {
				previous = covered_by[previous];
			}
			const bool attached = previous >= 0 && lines[previous].first_piece >= 0 && lines[previous].member_level && !lines[previous].decl && lines[previous].scope == info.scope;
			if (!attached) {
				bool has_decl = false;
				bool callable = false;
				for (int next = line; next < line_count && lines[next].first_piece >= 0 && lines[next].member_level; next++) {
					if (lines[next].decl) {
						has_decl = true;
						callable = lines[next].decl_callable;
						break;
					}
				}
				if (last_member_callable.has(info.scope) && (callable || last_member_callable[info.scope])) {
					info.member_blank = p_options.blank_lines_around_members;
				}
				if (!has_decl) {
					last_member_callable[info.scope] = false; // A free-standing comment.
				}
			}
			if (info.decl) {
				last_member_callable[info.scope] = info.decl_callable;
			}
		}
	}

	// `.gd` indentation is syntax, so it is converted level by level, never recomputed.
	String source_unit; // Empty: tabs.
	bool gd_convertible = true;
	if (!braced) {
		for (int line = 0; line < line_count && gd_convertible; line++) {
			const LineInfo &info = lines[line];
			if (info.first_piece < 0 || info.gd_depth == 0 || info.comment_only) {
				continue;
			}
			const Piece &first = pieces[info.first_piece];
			const String lead = source.substr(line_starts[line], first.start - line_starts[line]);
			if (lead.begins_with("\t")) {
				break; // Tab-indented.
			}
			const String unit_here = lead.length() % info.gd_depth == 0 ? String(" ").repeat(lead.length() / info.gd_depth) : String();
			if (unit_here.is_empty() || (!source_unit.is_empty() && unit_here != source_unit)) {
				gd_convertible = false;
			} else {
				source_unit = unit_here;
			}
		}
	}
	const String unit = p_options.use_spaces ? String(" ").repeat(MAX(1, p_options.indent_size)) : String("\t");
	auto convert_gd_indent = [&](const String &p_lead) {
		if (!gd_convertible) {
			return p_lead;
		}
		int depth = 0;
		int i = 0;
		if (source_unit.is_empty()) {
			while (i < p_lead.length() && p_lead[i] == '\t') {
				depth++;
				i++;
			}
		} else {
			while (p_lead.substr(i, source_unit.length()) == source_unit) {
				depth++;
				i += source_unit.length();
			}
		}
		return unit.repeat(depth) + p_lead.substr(i);
	};

	// Output.
	Vector<String> out;
	Vector<OutPieces> out_pieces; // Per output line, for alignment and wrapping.
	List<StackEntry> stack;
	int pending_blank = 0;
	int previous_code_line = -1;
	bool previous_opened_block = false;
	bool previous_is_lone_close = false;
	bool continuation = false;
	int continuation_level = 0;

	// `.gdb` indentation for an output line that starts with `p_first`.
	auto level_for = [&](const Piece &p_first) {
		if (continuation) {
			return continuation_level;
		}
		if (stack.is_empty()) {
			return 0;
		}
		const StackEntry &top = stack.back()->get();
		if (p_to_indented && p_first.kind == PIECE_TOKEN && p_first.closes == BRACKET_BLOCK && p_first.text == "pass") {
			return top.level + 1; // The `pass` of an empty block goes inside it.
		}
		if (p_to_indented && top.lambda_block && p_first.kind == PIECE_TOKEN && p_first.closes == BRACKET_BLOCK) {
			// What follows a lambda's body in `.gd` (`, 6))`) must dedent to the statement's level, which the
			// indentation stack knows, not to the line that opened the lambda inside brackets.
			return top.statement_level;
		}
		if (p_first.kind == PIECE_TOKEN && p_first.closes != BRACKET_NONE) {
			return top.level;
		}
		const int step = top.kind == BRACKET_BLOCK ? 1 : (top.kind == BRACKET_PAREN ? p_options.paren_continuation : p_options.bracket_continuation);
		return top.level + step;
	};
	auto trimmed = [](const String &p_text) {
		int length = p_text.length();
		while (length > 0 && (p_text[length - 1] == ' ' || p_text[length - 1] == '\t')) {
			length--;
		}
		return p_text.substr(0, length);
	};
	// `.gdb`: a block that spans lines gets its braces at the ends of lines, and a statement after a `}` its own line.
	auto breaks_after = [&](const Piece &p_piece, const Piece &p_next) {
		if (p_to_indented && p_piece.kind == PIECE_TOKEN && p_next.kind == PIECE_TOKEN) {
			// A block written on one line stays on one line (`if x: a()`), which also keeps `@warning_ignore` on the
			// statement's line, unless it holds another block, which `.gd` cannot nest on one line.
			if (p_piece.opens == BRACKET_BLOCK) {
				return p_piece.multiline_block || p_piece.holds_block;
			}
			if (p_next.closes == BRACKET_BLOCK) {
				// A lambda's body ends where the code after it continues: `return 1,` closes the lambda inline.
				return !p_next.lambda_block && (p_next.multiline_block || p_next.holds_block);
			}
			if (p_piece.closes == BRACKET_BLOCK && !p_piece.lambda_block) {
				return true;
			}
		}
		if (!braced || p_piece.kind != PIECE_TOKEN || p_next.kind != PIECE_TOKEN) {
			return false;
		}
		if (p_piece.opens == BRACKET_BLOCK) {
			return p_piece.multiline_block;
		}
		if (p_piece.closes == BRACKET_BLOCK) {
			switch (p_next.type) {
				case Token::ELSE:
				case Token::ELIF:
				case Token::COMMA:
				case Token::SEMICOLON:
				case Token::PERIOD:
				case Token::PARENTHESIS_CLOSE:
				case Token::BRACKET_CLOSE:
				case Token::BRACE_CLOSE:
					return false;
				default:
					// Another block closing right after: its own line if it spans lines.
					return p_next.closes != BRACKET_BLOCK || p_next.multiline_block;
			}
		}
		return p_next.closes == BRACKET_BLOCK && p_next.multiline_block;
	};

	for (int line = 0; line < line_count; line++) {
		const LineInfo &info = lines[line];
		if (info.first_piece < 0) {
			pending_blank++;
			continue;
		}
		const Piece &first = pieces[info.first_piece];
		const bool closes_first = first.kind == PIECE_TOKEN && first.closes != BRACKET_NONE;

		// Blank lines.
		int blank = MIN(pending_blank, p_options.max_blank_lines);
		const bool after_gd_header = !braced && previous_code_line >= 0 && lines[previous_code_line].gd_depth < info.gd_depth;
		if (out.is_empty() || previous_opened_block || closes_first || after_gd_header) {
			blank = 0;
		} else if (info.member_blank >= 0) {
			blank = info.member_blank;
		}
		pending_blank = 0;

		// K&R: `} else {` and `} elif x {` on one line.
		const bool joins_previous = braced && !p_to_indented && previous_is_lone_close && first.kind == PIECE_TOKEN && (first.type == Token::ELSE || first.type == Token::ELIF);
		String text;
		OutPieces record;
		int level = 0;
		if (joins_previous) {
			text = out[out.size() - 1] + " ";
			record = out_pieces[out_pieces.size() - 1];
			out.remove_at(out.size() - 1);
			out_pieces.remove_at(out_pieces.size() - 1);
			level = level_for(first);
		} else {
			for (int i = 0; i < blank; i++) {
				out.push_back(String());
				out_pieces.push_back(OutPieces());
			}
			if (braced) {
				level = level_for(first);
				text = unit.repeat(level);
			} else {
				text = convert_gd_indent(source.substr(line_starts[line], first.start - line_starts[line]));
			}
		}

		// The pieces on this line, and on the lines a multi-line piece runs into.
		int last_line = line;
		const Piece *previous = nullptr; // For line breaks.
		const Piece *emitted = nullptr; // For spacing: the last piece that produced text.
		const Piece *before_emitted = nullptr;
		bool lone_close = true;
		String deferred_comment; // `.gd`: a block comment with code after it goes to the end of the line.
		auto flush = [&]() {
			if (!deferred_comment.is_empty()) {
				text += "  " + deferred_comment;
				deferred_comment = String();
			}
			const String done = trimmed(text);
			if (!(p_to_indented && done.strip_edges().is_empty())) {
				out.push_back(done); // Converting to `.gd` drops lines that only held a `}`.
				out_pieces.push_back(record);
			}
			record.clear();
		};
		int i = info.first_piece;
		for (; i < pieces.size() && pieces[i].line <= last_line; i++) {
			const Piece &piece = pieces[i];
			if (previous != nullptr && breaks_after(*previous, piece)) {
				flush();
				// A member split off a `}` line (`}func f() {`) gets the blank lines it would get on its own line:
				// after a function or class, or when it is one.
				if (braced && !p_to_indented && previous->closes == BRACKET_BLOCK && !previous->lambda_block && piece.kind == PIECE_TOKEN && previous->partner >= 0) {
					auto callable_at = [&](int p_index) {
						int head = p_index;
						while (head < pieces.size() && pieces[head].kind == PIECE_TOKEN && (pieces[head].type == Token::ANNOTATION || pieces[head].type == Token::STATIC)) {
							head++;
						}
						return head < pieces.size() && (pieces[head].type == Token::FUNC || pieces[head].type == Token::CLASS);
					};
					int opener_head = previous->partner;
					while (opener_head > 0 && pieces[opener_head - 1].line == pieces[previous->partner].line) {
						opener_head--;
					}
					while (opener_head < previous->partner && pieces[opener_head].kind != PIECE_TOKEN) {
						opener_head++;
					}
					if (callable_at(i) || callable_at(opener_head)) {
						for (int blank_line = 0; blank_line < p_options.blank_lines_around_members; blank_line++) {
							out.push_back(String());
							out_pieces.push_back(OutPieces());
						}
					}
				}
				level = level_for(piece);
				text = unit.repeat(level);
				previous = nullptr;
				emitted = nullptr;
				lone_close = true;
			}
			const bool has_code_after = i + 1 < pieces.size() && pieces[i + 1].line <= MAX(last_line, piece.end_line) && pieces[i + 1].kind == PIECE_TOKEN;
			if (p_to_indented && piece.kind == PIECE_BLOCK_COMMENT && has_code_after) {
				deferred_comment = piece.text.replace("\n", " ");
			} else if (!piece.text.is_empty()) {
				if (emitted != nullptr) {
					text += _spacing(*emitted, piece, before_emitted, source, p_options, p_to_indented);
				}
				record.push_back(Vector2i(text.length(), i));
				text += piece.text;
				before_emitted = emitted;
				emitted = &pieces[i];
			}
			last_line = MAX(last_line, piece.end_line);
			if (!(piece.kind == PIECE_TOKEN && piece.closes == BRACKET_BLOCK)) {
				lone_close = false;
			}
			if (braced && piece.kind == PIECE_TOKEN) {
				if (piece.closes != BRACKET_NONE && !stack.is_empty()) {
					stack.pop_back();
				}
				if (piece.opens != BRACKET_NONE) {
					StackEntry entry;
					entry.kind = piece.opens;
					entry.opener_line = line;
					entry.level = level;
					entry.lambda_block = piece.lambda_block;
					if (piece.lambda_block) {
						// The statement's level: one inside the nearest enclosing block, or the top level.
						for (List<StackEntry>::Element *E = stack.back(); E; E = E->prev()) {
							if (E->get().kind == BRACKET_BLOCK) {
								entry.statement_level = E->get().level + 1;
								break;
							}
						}
					}
					stack.push_back(entry);
				}
			}
			previous = &pieces[i];
		}

		const Piece &last = pieces[i - 1];
		if (last.kind == PIECE_CONTINUATION) {
			if (!continuation) {
				continuation_level = level + p_options.paren_continuation;
			}
			continuation = true;
		} else {
			continuation = false;
		}
		previous_opened_block = braced && last.kind == PIECE_TOKEN && last.opens == BRACKET_BLOCK;
		previous_is_lone_close = braced && lone_close;
		if (!info.comment_only) {
			previous_code_line = line;
		}
		flush();
		line = last_line;
	}

	if (!p_to_indented) {
		if (p_options.align_assignments || p_options.align_comments || p_options.align_dictionary_values || p_options.align_match_branches) {
			_align(out, out_pieces, pieces, p_options, unit, braced);
		}
		if (p_options.max_line_length > 0) {
			const bool wrapped = _wrap(out, out_pieces, pieces, p_options, unit, braced);
			if (r_wrapped != nullptr) {
				*r_wrapped = wrapped;
			}
		}
	}

	String output = String("\n").join(out);
	if (!output.is_empty() && p_options.insert_final_newline) {
		output += "\n";
	}

	if (p_to_indented) {
		// A conversion produces different tokens by design; `convert()` checks it by comparing parse trees instead.
		result.output = crlf ? output.replace("\n", "\r\n") : output;
		return result;
	}

	// Safety: the same tokens, and still valid.
	Vector<Token> output_tokens;
	if (!_scan(output, braced, output_tokens, scan_error, scan_error_line)) {
		return fail(vformat("Internal formatter error: the formatted code does not tokenize (%s). Nothing was changed.", scan_error), scan_error_line);
	}
	const Vector<Token> compared = _without_trailing_commas(tokens);
	const Vector<Token> compared_output = _without_trailing_commas(output_tokens);
	const int difference = _first_difference(compared, compared_output);
	if (difference >= 0 && !OS::get_singleton()->get_environment("GDSCRIPT_FORMAT_DEBUG").is_empty()) {
		// For reports of this error: what the formatter produced, and where the token streams part.
		auto describe = [](const Vector<Token> &p_tokens, int p_index) {
			return p_index < p_tokens.size() ? vformat("%s '%s' at %d:%d", GDScriptTokenizer::get_token_name(p_tokens[p_index].type), p_tokens[p_index].source, p_tokens[p_index].start_line, p_tokens[p_index].start_column) : String("end");
		};
		OS::get_singleton()->printerr("--- formatted (rejected) ---\n%s\n--- input token %s, output token %s ---\n", output.utf8().get_data(), describe(compared, difference).utf8().get_data(), describe(compared_output, difference).utf8().get_data());
	}
	if (difference >= 0) {
		const int report_line = difference < compared.size() ? compared[difference].start_line : line_count;
		return fail("Internal formatter error: the formatted code would not mean the same thing. Nothing was changed.", report_line);
	}
	{
		GDScriptParser parser;
		if (parser.parse(output, parse_path, false) != OK) {
			return fail("Internal formatter error: the formatted code does not parse. Nothing was changed.", 0);
		}
	}

	result.output = crlf ? output.replace("\n", "\r\n") : output;
	return result;
}

// Conversion between `.gd` and `.gdb`.

namespace {

#ifdef DEBUG_ENABLED
// `c ? a : b` has one `.gd` spelling: `a if c else b`. Rewrites every one, nested ones from the inside, with
// parentheses only where a moved part holds another ternary.
String _rewrite_question_marks(const String &p_source, const Vector<GDScriptParser::QuestionMarkRecord> &p_records) {
	Vector<int> line_starts;
	line_starts.push_back(0);
	for (int i = 0; i < p_source.length(); i++) {
		if (p_source[i] == '\n') {
			line_starts.push_back(i + 1);
		}
	}
	// Clamped: for what ends the file without a line break, positions past the end are reported.
	auto offset_of = [&](int p_line, int p_column) {
		if (p_line > line_starts.size()) {
			return p_source.length();
		}
		return CLAMP(line_starts[CLAMP(p_line - 1, 0, line_starts.size() - 1)] + p_column - 1, 0, p_source.length());
	};
	struct Ternary {
		int start = 0;
		int question = 0;
		int colon = 0;
		int end = 0;
	};
	Vector<Ternary> ternaries;
	for (const GDScriptParser::QuestionMarkRecord &record : p_records) {
		const Ternary ternary = { offset_of(record.start_line, record.start_column), offset_of(record.question_line, record.question_column), offset_of(record.colon_line, record.colon_column), offset_of(record.end_line, record.end_column) };
		if (ternary.start < ternary.question && ternary.question < ternary.colon && ternary.colon < ternary.end) {
			ternaries.push_back(ternary);
		}
	}

	struct Rewriter {
		const String &source;
		const Vector<Ternary> &all;

		static bool parenthesized(const String &p_text) {
			if (!p_text.begins_with("(") || !p_text.ends_with(")")) {
				return false;
			}
			int depth = 0;
			for (int i = 0; i < p_text.length(); i++) {
				depth += p_text[i] == '(' ? 1 : (p_text[i] == ')' ? -1 : 0);
				if (depth == 0 && i < p_text.length() - 1) {
					return false; // `(a) + (b)`
				}
			}
			return true;
		}

		bool holds_ternary(int p_from, int p_to) const {
			for (const Ternary &ternary : all) {
				if (ternary.start >= p_from && ternary.end <= p_to) {
					return true;
				}
			}
			return false;
		}

		String emit(int p_from, int p_to) const {
			String out;
			int i = p_from;
			while (i < p_to) {
				int outer = -1;
				for (int k = 0; k < all.size(); k++) {
					if (all[k].start == i && all[k].end <= p_to && (outer < 0 || all[k].end > all[outer].end)) {
						outer = k;
					}
				}
				if (outer < 0) {
					out += source[i];
					i++;
					continue;
				}
				const Ternary &ternary = all[outer];
				String condition = emit(ternary.start, ternary.question).strip_edges();
				String when_true = emit(ternary.question + 1, ternary.colon).strip_edges();
				const String when_false = emit(ternary.colon + 1, ternary.end).strip_edges();
				if (holds_ternary(ternary.start, ternary.question) && !parenthesized(condition)) {
					condition = "(" + condition + ")";
				}
				if (holds_ternary(ternary.question + 1, ternary.colon) && !parenthesized(when_true)) {
					when_true = "(" + when_true + ")";
				}
				out += when_true + " if " + condition + " else " + when_false;
				i = ternary.end;
			}
			return out;
		}
	};
	return Rewriter{ p_source, ternaries }.emit(0, p_source.length());
}
#endif // DEBUG_ENABLED

} // namespace

GDScriptFormatter::Result GDScriptFormatter::convert(const String &p_source, const String &p_path, bool p_keep_lines, const Options &p_options) {
	Result result;
	auto fail = [&](const String &p_message, int p_line) {
		result.error = ERR_PARSE_ERROR;
		result.message = p_message;
		result.line = p_line;
		return result;
	};
#ifndef DEBUG_ENABLED
	return fail("Converting scripts needs a debug or editor build.", 0);
#else
	if (!GDScriptTokenizer::is_braced_path("x.gdb")) {
		return fail("Braced GDScript is not compiled into this build.", 0);
	}
	const bool from_braced = GDScriptTokenizer::is_braced_path(p_path);
	const String base = p_path.is_empty() ? String("res://convert") : p_path.get_basename();
	const String gd_path = base + ".gd";
	const String gdb_path = base + ".gdb";
	const bool crlf = p_options.use_crlf || p_source.contains("\r\n");
	const String original_source = p_source.replace("\r\n", "\n");

	// The source's tree, and for a `.gd` source where its blocks start and end.
	String source = original_source;
	GDScriptParser source_parser;
	Vector<GDScriptParser::BlockRecord> blocks;
	Vector<GDScriptParser::QuestionMarkRecord> question_marks;
	Vector<GDScriptParser::ArrowReturnRecord> arrow_returns;
	if (from_braced) {
		source_parser.set_question_mark_log(&question_marks);
		source_parser.set_arrow_return_log(&arrow_returns);
	} else {
		source_parser.set_block_log(&blocks);
	}
	if (source_parser.parse(source, from_braced ? gdb_path : gd_path, false) != OK) {
		const List<GDScriptParser::ParserError> &errors = source_parser.get_errors();
		return errors.is_empty() ? fail("The script has syntax errors.", 0) : fail(errors.front()->get().message, errors.front()->get().start_line);
	}

	// An arrow lambda may return a `void` call in `.gdb` (`() => print(1)`), which `.gd` cannot say as a `return`. Its
	// analyzed meaning (run the call, return `null`) is the block `() => { print(1) }`, so write it that way first.
	if (!arrow_returns.is_empty()) {
		GDScriptAnalyzer analyzer(&source_parser);
		analyzer.analyze();
		Vector<int> line_starts;
		line_starts.push_back(0);
		for (int i = 0; i < source.length(); i++) {
			if (source[i] == '\n') {
				line_starts.push_back(i + 1);
			}
		}
		String rewritten = source;
		for (int i = arrow_returns.size() - 1; i >= 0; i--) {
			const GDScriptParser::ArrowReturnRecord &record = arrow_returns[i];
			if (!record.node->void_return) {
				continue;
			}
			if (record.arrow_line < 1 || record.arrow_line > line_starts.size() || record.end_line < 1) {
				continue;
			}
			const int arrow = line_starts[record.arrow_line - 1] + record.arrow_column - 1;
			// The end may be on a line past the last, for code that ends the file without a line break.
			const int end = record.end_line > line_starts.size() ? source.length() : MIN(source.length(), line_starts[record.end_line - 1] + record.end_column - 1);
			rewritten = rewritten.substr(0, end) + " }" + rewritten.substr(end);
			rewritten = rewritten.substr(0, arrow + 2) + " {" + rewritten.substr(arrow + 2);
		}
		if (rewritten != source) {
			source = rewritten;
			question_marks.clear();
			arrow_returns.clear();
			if (source_parser.parse(source, gdb_path, false) != OK) {
				return fail("Internal converter error: rewriting an arrow lambda broke the code.", 0);
			}
		}
	}
	const String tree = GDScriptParser::TreePrinter().get_tree_text(source_parser);

	String output;
	if (from_braced) {
		const String rewritten = question_marks.is_empty() ? source : _rewrite_question_marks(source, question_marks);
		const Result indented = _format(rewritten, gdb_path, p_options, true);
		if (indented.error != OK) {
			return indented;
		}
		output = indented.output.replace("\r\n", "\n");
	} else {
		// Each block's `:` becomes ` {`, and ` }` goes after its last token, so every line stays where it was.
		Vector<int> line_starts;
		line_starts.push_back(0);
		for (int i = 0; i < source.length(); i++) {
			if (source[i] == '\n') {
				line_starts.push_back(i + 1);
			}
		}
		auto offset_of = [&](int p_line, int p_column) {
			if (p_line > line_starts.size()) {
				return source.length(); // Past the end of a file without a final line break.
			}
			return CLAMP(line_starts[CLAMP(p_line - 1, 0, line_starts.size() - 1)] + p_column - 1, 0, source.length());
		};
		struct Edit {
			int offset = 0;
			int order = 0;
			bool replaces_colon = false;
		};
		Vector<Edit> edits;
		for (int i = 0; i < blocks.size(); i++) {
			// Blocks are logged as they finish, inner first, so `order` puts an inner `}` before an outer one.
			edits.push_back({ offset_of(blocks[i].colon_line, blocks[i].colon_column), i, true });
			edits.push_back({ offset_of(blocks[i].end_line, blocks[i].end_column), i, false });
		}
		struct EditOrder {
			bool operator()(const Edit &p_a, const Edit &p_b) const {
				return p_a.offset == p_b.offset ? p_a.order < p_b.order : p_a.offset < p_b.offset;
			}
		};
		edits.sort_custom<EditOrder>();
		String converted;
		int copied = 0;
		for (const Edit &edit : edits) {
			if (edit.offset < copied || edit.offset > source.length()) {
				return fail("Internal converter error: overlapping blocks.", 0);
			}
			converted += source.substr(copied, edit.offset - copied);
			copied = edit.offset;
			if (edit.replaces_colon) {
				if (source[edit.offset] != ':') {
					return fail("Internal converter error: a block does not start with \":\".", 0);
				}
				const bool spaced = edit.offset > 0 && (source[edit.offset - 1] == ' ' || source[edit.offset - 1] == '\t');
				converted += spaced ? "{" : " {";
				copied++;
			} else {
				converted += " }";
			}
		}
		converted += source.substr(copied);
		output = converted;
		if (!p_keep_lines) {
			const Result formatted = format(output, gdb_path, p_options);
			if (formatted.error != OK) {
				return fail("Internal converter error: the converted code cannot be formatted: " + formatted.message, formatted.line);
			}
			output = formatted.output.replace("\r\n", "\n");
		}
	}

	// The result must mean exactly what the source meant.
	GDScriptParser check;
	if (check.parse(output, from_braced ? gd_path : gdb_path, false) != OK) {
		const List<GDScriptParser::ParserError> &errors = check.get_errors();
		const String reason = errors.is_empty() ? String() : vformat(" (line %d: %s)", errors.front()->get().start_line, errors.front()->get().message);
		return fail("Internal converter error: the converted code does not parse" + reason + ". Nothing was changed.", errors.is_empty() ? 0 : errors.front()->get().start_line);
	}
	if (GDScriptParser::TreePrinter().get_tree_text(check) != tree) {
		return fail("Internal converter error: the converted code would not mean the same thing. Nothing was changed.", 0);
	}
	result.output = crlf ? output.replace("\n", "\r\n") : output;
	return result;
#endif // DEBUG_ENABLED
}

// EditorConfig.

namespace {

void _expand_braces(const String &p_pattern, Vector<String> &r_patterns) {
	const int open = p_pattern.find_char('{');
	const int close = open >= 0 ? p_pattern.find_char('}', open) : -1;
	if (open < 0 || close < 0) {
		r_patterns.push_back(p_pattern);
		return;
	}
	const String head = p_pattern.substr(0, open);
	const String tail = p_pattern.substr(close + 1);
	for (const String &alternative : p_pattern.substr(open + 1, close - open - 1).split(",")) {
		_expand_braces(head + alternative + tail, r_patterns);
	}
}

bool _section_matches(const String &p_section, const String &p_relative_path) {
	String pattern = p_section;
	String subject = p_relative_path;
	if (!pattern.contains_char('/')) {
		subject = p_relative_path.get_file();
	} else if (pattern.begins_with("/")) {
		pattern = pattern.substr(1);
	}
	Vector<String> patterns;
	_expand_braces(pattern, patterns);
	for (const String &candidate : patterns) {
		if (subject.match(candidate.replace("**", "*"))) {
			return true;
		}
	}
	return false;
}

} // namespace

String GDScriptFormatter::get_editorconfig_keys() {
	return "gdscript_preset = godot | js\n"
		   "gdscript_operator_style = keep | words | symbols\n"
		   "gdscript_comment_style = keep | hash | slashes\n"
		   "gdscript_elif_style = keep | elif | else_if\n"
		   "gdscript_spaces_in_braces = true | false\n"
		   "gdscript_paren_continuation = <levels>\n"
		   "gdscript_bracket_continuation = <levels>\n"
		   "gdscript_blank_lines_around_members = <count>\n"
		   "gdscript_max_blank_lines = <count>\n"
		   "gdscript_trailing_commas = keep | literals | all\n"
		   "gdscript_align_assignments = true | false\n"
		   "gdscript_align_comments = true | false\n"
		   "gdscript_align_dictionary_values = true | false\n"
		   "gdscript_align_match_branches = true | false\n";
}

GDScriptFormatter::Options GDScriptFormatter::get_options_for_path(const String &p_path, const String &p_preset_override) {
	// Collect `.editorconfig` files from the file's directory up to the first `root = true`.
	HashMap<String, String> values;
	Vector<String> directories;
	String directory = p_path.get_base_dir();
	while (!directory.is_empty()) {
		directories.push_back(directory);
		const String parent = directory.get_base_dir();
		if (parent == directory) {
			break;
		}
		directory = parent;
	}

	Vector<String> files;
	for (const String &dir : directories) {
		const String file = dir.path_join(".editorconfig");
		if (!FileAccess::exists(file)) {
			continue;
		}
		files.push_back(file);
		const String content = FileAccess::get_file_as_string(file);
		bool root = false;
		for (const String &raw : content.split("\n")) {
			const String line = raw.strip_edges();
			if (!line.begins_with("[") && line.contains_char('=') && line.get_slicec('=', 0).strip_edges().to_lower() == "root") {
				root = line.get_slicec('=', 1).strip_edges().to_lower() == "true";
				break;
			}
			if (line.begins_with("[")) {
				break;
			}
		}
		if (root) {
			break;
		}
	}

	// Apply from the farthest file to the nearest; later sections win.
	for (int i = files.size() - 1; i >= 0; i--) {
		const String &file = files[i];
		const String relative = p_path.substr(file.get_base_dir().length()).trim_prefix("/");
		bool active = false;
		for (const String &raw : FileAccess::get_file_as_string(file).split("\n")) {
			const String line = raw.strip_edges();
			if (line.is_empty() || line.begins_with("#") || line.begins_with(";")) {
				continue;
			}
			if (line.begins_with("[") && line.ends_with("]")) {
				active = _section_matches(line.substr(1, line.length() - 2), relative);
				continue;
			}
			if (active && line.contains_char('=')) {
				values[line.get_slicec('=', 0).strip_edges().to_lower()] = line.substr(line.find_char('=') + 1).strip_edges().to_lower();
			}
		}
	}

	const bool braced = GDScriptTokenizer::is_braced_path(p_path);
	String preset = p_preset_override;
	if (preset.is_empty()) {
		preset = values.has("gdscript_preset") ? values["gdscript_preset"] : get_default_preset(braced);
	}
	Options options = get_preset(preset);

	auto get_int = [&](const String &p_key, int &r_value) {
		if (values.has(p_key) && values[p_key].is_valid_int()) {
			r_value = MAX(0, (int)values[p_key].to_int());
		}
	};
	auto get_bool = [&](const String &p_key, bool &r_value) {
		if (values.has(p_key)) {
			r_value = values[p_key] == "true";
		}
	};
	if (values.has("indent_style")) {
		options.use_spaces = values["indent_style"] == "space";
	}
	get_int("tab_width", options.indent_size);
	get_int("indent_size", options.indent_size);
	get_int("max_line_length", options.max_line_length);
	if (values.has("max_line_length") && values["max_line_length"] == "off") {
		options.max_line_length = 0;
	}
	get_bool("insert_final_newline", options.insert_final_newline);
	if (values.has("end_of_line")) {
		options.use_crlf = values["end_of_line"] == "crlf";
	}
	if (values.has("gdscript_operator_style")) {
		const String &style = values["gdscript_operator_style"];
		options.operator_style = style == "words" ? OPERATOR_STYLE_WORDS : (style == "symbols" ? OPERATOR_STYLE_SYMBOLS : OPERATOR_STYLE_KEEP);
	}
	if (values.has("gdscript_comment_style")) {
		const String &style = values["gdscript_comment_style"];
		options.comment_style = style == "hash" ? COMMENT_STYLE_HASH : (style == "slashes" ? COMMENT_STYLE_SLASHES : COMMENT_STYLE_KEEP);
	}
	if (values.has("gdscript_elif_style")) {
		const String &style = values["gdscript_elif_style"];
		options.elif_style = style == "elif" ? ELIF_STYLE_ELIF : (style == "else_if" ? ELIF_STYLE_ELSE_IF : ELIF_STYLE_KEEP);
	}
	get_bool("gdscript_spaces_in_braces", options.spaces_in_braces);
	get_int("gdscript_paren_continuation", options.paren_continuation);
	get_int("gdscript_bracket_continuation", options.bracket_continuation);
	get_int("gdscript_blank_lines_around_members", options.blank_lines_around_members);
	get_int("gdscript_max_blank_lines", options.max_blank_lines);
	if (values.has("gdscript_trailing_commas")) {
		const String &style = values["gdscript_trailing_commas"];
		options.trailing_commas = style == "all" ? TRAILING_COMMAS_ALL : (style == "literals" ? TRAILING_COMMAS_LITERALS : TRAILING_COMMAS_KEEP);
	}
	get_bool("gdscript_align_assignments", options.align_assignments);
	get_bool("gdscript_align_comments", options.align_comments);
	get_bool("gdscript_align_dictionary_values", options.align_dictionary_values);
	get_bool("gdscript_align_match_branches", options.align_match_branches);
	return options;
}

// Line diffs, for `--range` and `--diff`.

namespace {

// `p_after[r_to_after]` replaces `p_before[from, to)`.
struct LineHunk {
	int from = 0;
	int to = 0;
	int after_from = 0;
	int after_to = 0;
};

Vector<LineHunk> _line_hunks(const Vector<String> &p_before, const Vector<String> &p_after) {
	Vector<LineHunk> hunks;
	int prefix = 0;
	while (prefix < p_before.size() && prefix < p_after.size() && p_before[prefix] == p_after[prefix]) {
		prefix++;
	}
	int suffix = 0;
	while (suffix < p_before.size() - prefix && suffix < p_after.size() - prefix && p_before[p_before.size() - 1 - suffix] == p_after[p_after.size() - 1 - suffix]) {
		suffix++;
	}
	const int n = p_before.size() - prefix - suffix;
	const int m = p_after.size() - prefix - suffix;
	if (n == 0 && m == 0) {
		return hunks;
	}
	if ((int64_t)(n + 1) * (m + 1) > 4000000) {
		// Too large to align line by line: one hunk.
		hunks.push_back({ prefix, prefix + n, prefix, prefix + m });
		return hunks;
	}
	// Longest common subsequence of the middle, then the runs between its lines.
	Vector<int> table;
	table.resize((n + 1) * (m + 1));
	table.fill(0);
	auto at = [&](int i, int j) -> int & { return table.write[i * (m + 1) + j]; };
	for (int i = n - 1; i >= 0; i--) {
		for (int j = m - 1; j >= 0; j--) {
			at(i, j) = p_before[prefix + i] == p_after[prefix + j] ? at(i + 1, j + 1) + 1 : MAX(at(i + 1, j), at(i, j + 1));
		}
	}
	int i = 0;
	int j = 0;
	LineHunk open = { -1, -1, -1, -1 };
	auto close = [&]() {
		if (open.from < 0) {
			return;
		}
		if (open.to - open.from == open.after_to - open.after_from) {
			// Lines changed one for one (re-indented, re-spaced): a hunk each, so a range takes exactly its own lines.
			for (int k = 0; k < open.to - open.from; k++) {
				hunks.push_back({ open.from + k, open.from + k + 1, open.after_from + k, open.after_from + k + 1 });
			}
		} else {
			hunks.push_back(open);
		}
		open = { -1, -1, -1, -1 };
	};
	while (i < n || j < m) {
		if (i < n && j < m && p_before[prefix + i] == p_after[prefix + j]) {
			close();
			i++;
			j++;
			continue;
		}
		if (open.from < 0) {
			open = { prefix + i, prefix + i, prefix + j, prefix + j };
		}
		if (j < m && (i == n || at(i, j + 1) >= at(i + 1, j))) {
			j++;
			open.after_to = prefix + j;
		} else {
			i++;
			open.to = prefix + i;
		}
	}
	close();
	return hunks;
}

} // namespace

GDScriptFormatter::Result GDScriptFormatter::format_ranges(const String &p_source, const String &p_path, const Options &p_options, const Vector<Vector2i> &p_line_ranges) {
	Result result = format(p_source, p_path, p_options);
	if (result.error != OK || p_line_ranges.is_empty()) {
		return result;
	}
	const bool crlf = p_source.contains("\r\n");
	const Vector<String> before = p_source.replace("\r\n", "\n").split("\n");
	const Vector<String> after = result.output.replace("\r\n", "\n").split("\n");
	Vector<String> lines;
	int copied = 0;
	for (const LineHunk &hunk : _line_hunks(before, after)) {
		// 1-based inclusive ranges; a hunk that only inserts touches the line it is inserted before.
		bool inside = false;
		for (const Vector2i &range : p_line_ranges) {
			const int first = range.x - 1;
			const int last = range.y - 1;
			inside = inside || (hunk.from == hunk.to ? (hunk.from >= first && hunk.from <= last + 1) : (hunk.from <= last && hunk.to - 1 >= first));
		}
		for (; copied < hunk.from; copied++) {
			lines.push_back(before[copied]);
		}
		if (inside) {
			for (int k = hunk.after_from; k < hunk.after_to; k++) {
				lines.push_back(after[k]);
			}
		} else {
			for (int k = hunk.from; k < hunk.to; k++) {
				lines.push_back(before[k]);
			}
		}
		copied = hunk.to;
	}
	for (; copied < before.size(); copied++) {
		lines.push_back(before[copied]);
	}
	String output = String("\n").join(lines);
	{
		GDScriptParser parser;
		if (parser.parse(output, p_path.is_empty() ? String("res://format.gd") : p_path, false) != OK) {
			result.error = ERR_PARSE_ERROR;
			result.message = "Formatting only these lines would leave the script invalid; format the whole file.";
			result.line = 0;
			result.output = String();
			return result;
		}
	}
	result.output = crlf ? output.replace("\n", "\r\n") : output;
	return result;
}

String GDScriptFormatter::unified_diff(const String &p_before, const String &p_after, const String &p_path) {
	const Vector<String> before = p_before.replace("\r\n", "\n").split("\n");
	const Vector<String> after = p_after.replace("\r\n", "\n").split("\n");
	const Vector<LineHunk> hunks = _line_hunks(before, after);
	if (hunks.is_empty()) {
		return String();
	}
	const int context = 3;
	String diff = vformat("--- %s\n+++ %s\n", p_path, p_path);
	int h = 0;
	while (h < hunks.size()) {
		// Merge hunks whose contexts touch.
		int last = h;
		while (last + 1 < hunks.size() && hunks[last + 1].from - hunks[last].to <= 2 * context) {
			last++;
		}
		const int start = MAX(0, hunks[h].from - context);
		const int end = MIN(before.size(), hunks[last].to + context);
		const int after_start = hunks[h].after_from - (hunks[h].from - start);
		const int after_end = hunks[last].after_to + (end - hunks[last].to);
		diff += vformat("@@ -%d,%d +%d,%d @@\n", start + 1, end - start, after_start + 1, after_end - after_start);
		int line = start;
		for (int k = h; k <= last; k++) {
			for (; line < hunks[k].from; line++) {
				diff += " " + before[line] + "\n";
			}
			// Hunks that touch print as one: the removed lines, then the added ones.
			int run_end = k;
			while (run_end < last && hunks[run_end + 1].from == hunks[run_end].to) {
				run_end++;
			}
			for (int b = hunks[k].from; b < hunks[run_end].to; b++) {
				diff += "-" + before[b] + "\n";
			}
			for (int a = hunks[k].after_from; a < hunks[run_end].after_to; a++) {
				diff += "+" + after[a] + "\n";
			}
			line = hunks[run_end].to;
			k = run_end;
		}
		for (; line < end; line++) {
			diff += " " + before[line] + "\n";
		}
		h = last + 1;
	}
	return diff;
}

// Command line.

namespace {

void _collect_scripts(const String &p_path, Vector<String> &r_files) {
	if (FileAccess::exists(p_path)) {
		r_files.push_back(p_path);
		return;
	}
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (name.begins_with(".")) {
			continue; // `.`, `..`, `.godot`, `.git`.
		}
		const String full = p_path.path_join(name);
		if (dir->current_is_dir()) {
			_collect_scripts(full, r_files);
		} else if (GDScriptLanguage::is_gdscript_path(name)) {
			r_files.push_back(full);
		}
	}
	dir->list_dir_end();
}

String _absolute(const String &p_path) {
	if (p_path.is_absolute_path()) {
		return p_path.simplify_path();
	}
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	return dir->get_current_dir().path_join(p_path).simplify_path();
}

String _read_stdin() {
	Vector<uint8_t> bytes;
	for (;;) {
		const PackedByteArray chunk = OS::get_singleton()->get_stdin_buffer(65536);
		if (chunk.is_empty()) {
			break;
		}
		bytes.append_array(chunk);
	}
	return String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size());
}

} // namespace

int GDScriptFormatter::run_server() {
	// Each request and response is a line with the byte length of a JSON object, then the object.
	Logger::set_flush_stdout_on_print(true);
	for (;;) {
		const String header = OS::get_singleton()->get_stdin_string(64).strip_edges();
		if (header.is_empty()) {
			return 0; // End of input: the editor closed the pipe.
		}
		const int length = header.to_int();
		const PackedByteArray payload = length > 0 ? OS::get_singleton()->get_stdin_buffer(length) : PackedByteArray();
		if (payload.size() < length) {
			return 0;
		}

		Dictionary response;
		JSON json;
		if (length <= 0 || json.parse(String::utf8(reinterpret_cast<const char *>(payload.ptr()), payload.size())) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
			response["ok"] = false;
			response["line"] = 0;
			response["message"] = "The request is not a JSON object.";
		} else {
			const Dictionary request = json.get_data();
			response["id"] = request.get("id", Variant());
			const String path = _absolute(request.get("path", "stdin.gd"));
			const String mode = request.get("mode", "format");
			const String text = request.get("text", "");
			Result result;
			if (mode == "version") {
				result.output = itos(get_braces_grammar_version());
			} else if (mode == "to-gd" || mode == "to-gdb") {
				const bool to_braced = mode == "to-gdb";
				const String target = path.get_basename() + (to_braced ? ".gdb" : ".gd");
				result = convert(text, path, request.get("keep_lines", false), get_options_for_path(target, request.get("preset", "")));
			} else {
				Options options = get_options_for_path(path, request.get("preset", ""));
				const String indent_style = request.get("indent_style", "");
				if (indent_style == "tab" || indent_style == "space") {
					options.use_spaces = indent_style == "space";
				}
				if ((int)request.get("indent_size", 0) > 0) {
					options.indent_size = request.get("indent_size", 0);
				}
				Vector<Vector2i> ranges;
				const Array raw_ranges = request.get("ranges", Array());
				for (const Variant &raw : raw_ranges) {
					const Array range = raw;
					if (range.size() == 2 && (int)range[0] >= 1 && (int)range[1] >= (int)range[0]) {
						ranges.push_back(Vector2i(range[0], range[1]));
					}
				}
				result = ranges.is_empty() ? format(text, path, options) : format_ranges(text, path, options, ranges);
			}
			response["ok"] = result.error == OK;
			if (result.error == OK) {
				response["text"] = result.output;
				if (!result.note.is_empty()) {
					response["note"] = result.note;
				}
			} else {
				response["line"] = result.line;
				response["message"] = result.message;
			}
		}
		const CharString out = JSON::stringify(response).utf8();
		OS::get_singleton()->print("%d\n%s", (int)out.length(), out.get_data());
	}
}

int GDScriptFormatter::get_braces_grammar_version() {
#ifdef GDSCRIPT_BRACES_ENABLED
	return BRACES_GRAMMAR_VERSION;
#else
	return 0;
#endif // GDSCRIPT_BRACES_ENABLED
}

int GDScriptFormatter::run_cli(const List<String> &p_args) {
	bool check = false;
	bool diff = false;
	Vector<Vector2i> ranges;
	bool use_stdin = false;
	String stdin_path;
	String preset;
	String indent_style; // Overrides `.editorconfig`; editors pass their own effective setting.
	int indent_size = 0;
	Vector<String> paths;

	bool ours = false;
	for (const List<String>::Element *E = p_args.front(); E; E = E->next()) {
		const String &arg = E->get();
		if (!ours) {
			ours = arg == "--gdscript-format";
			continue;
		}
		if (arg == "--check") {
			check = true;
		} else if (arg == "--diff") {
			diff = true;
		} else if (arg == "--range" && E->next()) {
			E = E->next();
			const String range = E->get();
			const int first = range.get_slicec(':', 0).to_int();
			const int last = range.contains_char(':') ? range.get_slicec(':', 1).to_int() : first;
			if (first < 1 || last < first) {
				OS::get_singleton()->printerr("--range %s: expected <first line>:<last line>, from 1.\n", range.utf8().get_data());
				return 1;
			}
			ranges.push_back(Vector2i(first, last));
		} else if (arg == "--stdin") {
			use_stdin = true;
		} else if (arg == "--stdin-path" && E->next()) {
			E = E->next();
			stdin_path = E->get();
		} else if (arg == "--preset" && E->next()) {
			E = E->next();
			preset = E->get();
		} else if (arg == "--indent-style" && E->next()) {
			E = E->next();
			indent_style = E->get();
		} else if (arg == "--indent-size" && E->next()) {
			E = E->next();
			indent_size = E->get().to_int();
		} else if (!arg.begins_with("-")) {
			paths.push_back(arg);
		}
	}

	auto options_for = [&](const String &p_path) {
		Options options = get_options_for_path(p_path, preset);
		if (indent_style == "tab" || indent_style == "space") {
			options.use_spaces = indent_style == "space";
		}
		if (indent_size > 0) {
			options.indent_size = indent_size;
		}
		return options;
	};

	if (use_stdin) {
		const String source = _read_stdin();
		const String path = stdin_path.is_empty() ? String("stdin.gd") : _absolute(stdin_path);
		const Result result = ranges.is_empty() ? format(source, path, options_for(path)) : format_ranges(source, path, options_for(path), ranges);
		if (result.error != OK) {
			OS::get_singleton()->printerr("%s:%d: %s\n", path.utf8().get_data(), result.line, result.message.utf8().get_data());
			return 1;
		}
		if (!result.note.is_empty()) {
			OS::get_singleton()->printerr("%s: %s\n", path.utf8().get_data(), result.note.utf8().get_data());
		}
		OS::get_singleton()->print("%s", result.output.utf8().get_data());
		return 0;
	}

	if (paths.is_empty()) {
		OS::get_singleton()->printerr("Usage: godot --headless --gdscript-format [--check [--diff]] [--preset godot|js] [--indent-style tab|space] [--indent-size <n>] [--stdin [--stdin-path <path>] [--range <first>:<last>]...] <files or directories>\n");
		return 1;
	}

	Vector<String> files;
	for (const String &path : paths) {
		_collect_scripts(_absolute(path), files);
	}

	int errors = 0;
	int changed = 0;
	for (const String &file : files) {
		Error err = OK;
		const String source = FileAccess::get_file_as_string(file, &err);
		if (err != OK) {
			OS::get_singleton()->printerr("%s: cannot read the file.\n", file.utf8().get_data());
			errors++;
			continue;
		}
		const Result result = format(source, file, options_for(file));
		if (result.error != OK) {
			OS::get_singleton()->printerr("%s:%d: %s\n", file.utf8().get_data(), result.line, result.message.utf8().get_data());
			errors++;
			continue;
		}
		if (!result.note.is_empty()) {
			OS::get_singleton()->printerr("%s: %s\n", file.utf8().get_data(), result.note.utf8().get_data());
		}
		if (result.output == source) {
			continue;
		}
		changed++;
		if (check) {
			OS::get_singleton()->print("Would reformat %s\n", file.utf8().get_data());
			if (diff) {
				OS::get_singleton()->print("%s", unified_diff(source, result.output, file).utf8().get_data());
			}
			continue;
		}
		Ref<FileAccess> f = FileAccess::open(file, FileAccess::WRITE, &err);
		if (f.is_null()) {
			OS::get_singleton()->printerr("%s: cannot write the file.\n", file.utf8().get_data());
			errors++;
			continue;
		}
		f->store_string(result.output);
		OS::get_singleton()->print("Formatted %s\n", file.utf8().get_data());
	}

	OS::get_singleton()->print("%d file(s) checked, %d %s, %d error(s).\n", (int)files.size(), changed, check ? "would change" : "changed", errors);
	return (errors > 0 || (check && changed > 0)) ? 1 : 0;
}

int GDScriptFormatter::run_convert_cli(const List<String> &p_args) {
	String direction;
	bool keep_lines = false;
	bool replace = false;
	bool use_stdin = false;
	String stdin_path;
	String preset;
	Vector<String> paths;
	bool ours = false;
	for (const List<String>::Element *E = p_args.front(); E; E = E->next()) {
		const String &arg = E->get();
		if (!ours) {
			ours = arg == "--gdscript-convert";
			continue;
		}
		if (direction.is_empty() && (arg == "to-gdb" || arg == "to-gd")) {
			direction = arg;
		} else if (arg == "--keep-lines") {
			keep_lines = true;
		} else if (arg == "--replace") {
			replace = true;
		} else if (arg == "--stdin") {
			use_stdin = true;
		} else if (arg == "--stdin-path" && E->next()) {
			E = E->next();
			stdin_path = E->get();
		} else if (arg == "--preset" && E->next()) {
			E = E->next();
			preset = E->get();
		} else if (!arg.begins_with("-")) {
			paths.push_back(arg);
		}
	}
	if (direction.is_empty() || (paths.is_empty() && !use_stdin)) {
		OS::get_singleton()->printerr("Usage: godot --headless --gdscript-convert <to-gdb|to-gd> [--keep-lines] [--replace] [--preset godot|js] <files or directories>\n"
									  "       godot --headless --gdscript-convert <to-gdb|to-gd> [--keep-lines] [--preset godot|js] --stdin [--stdin-path <path>]\n");
		return 1;
	}
	const bool to_braced = direction == "to-gdb";

	if (use_stdin) {
		// The source's path: its folder picks the `.editorconfig` that styles the result, as for the file beside it.
		const String path = stdin_path.is_empty() ? String(to_braced ? "stdin.gd" : "stdin.gdb") : _absolute(stdin_path);
		if (GDScriptTokenizer::is_braced_path(path) == to_braced) {
			OS::get_singleton()->printerr("%s: already %s.\n", path.utf8().get_data(), to_braced ? "braced" : "indented");
			return 1;
		}
		const String target = path.get_basename() + (to_braced ? ".gdb" : ".gd");
		const Result result = convert(_read_stdin(), path, keep_lines, get_options_for_path(target, preset));
		if (result.error != OK) {
			OS::get_singleton()->printerr("%s:%d: %s\n", path.utf8().get_data(), result.line, result.message.utf8().get_data());
			return 1;
		}
		OS::get_singleton()->print("%s", result.output.utf8().get_data());
		return 0;
	}

	Vector<String> files;
	for (const String &path : paths) {
		_collect_scripts(_absolute(path), files);
	}
	int errors = 0;
	int converted = 0;
	for (const String &file : files) {
		if (GDScriptTokenizer::is_braced_path(file) == to_braced) {
			continue; // Already in the target form.
		}
		const String target = file.get_basename() + (to_braced ? ".gdb" : ".gd");
		if (FileAccess::exists(target)) {
			OS::get_singleton()->printerr("%s: %s already exists; skipped.\n", file.utf8().get_data(), target.get_file().utf8().get_data());
			errors++;
			continue;
		}
		Error err = OK;
		const String source = FileAccess::get_file_as_string(file, &err);
		if (err != OK) {
			OS::get_singleton()->printerr("%s: cannot read the file.\n", file.utf8().get_data());
			errors++;
			continue;
		}
		const Result result = convert(source, file, keep_lines, get_options_for_path(target, preset));
		if (result.error != OK) {
			OS::get_singleton()->printerr("%s:%d: %s\n", file.utf8().get_data(), result.line, result.message.utf8().get_data());
			errors++;
			continue;
		}
		Ref<FileAccess> f = FileAccess::open(target, FileAccess::WRITE, &err);
		if (f.is_null()) {
			OS::get_singleton()->printerr("%s: cannot write the file.\n", target.utf8().get_data());
			errors++;
			continue;
		}
		f->store_string(result.output);
		f.unref();
		if (replace) {
			// Moving the `.uid` file keeps references by UID working; references by path still name the old file.
			Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
			if (FileAccess::exists(file + ".uid")) {
				dir->rename(file + ".uid", target + ".uid");
			}
			dir->remove(file);
		}
		converted++;
		OS::get_singleton()->print("Converted %s -> %s\n", file.utf8().get_data(), target.get_file().utf8().get_data());
	}
	OS::get_singleton()->print("%d file(s) converted, %d error(s).\n", converted, errors);
	return errors > 0 ? 1 : 0;
}
