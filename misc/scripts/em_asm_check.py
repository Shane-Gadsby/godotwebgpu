#!/usr/bin/env python3
"""Reject an EM_ASM body that the C preprocessor will split on a comma.

EM_ASM's first argument is stringified (`#code`) and handed to Emscripten as
JavaScript. Braces do not protect commas from macro argument splitting -- only
parentheses do -- so a comma at the top level of the body ends the first macro
argument, and everything after it becomes a variadic argument that the compiler
then tries to read as C++:

    EM_ASM({ window.stats = { a : $0, b : $1 }; }, x, y);
    //                                ^ ends the body; `b : $1` is compiled as C++

That fails with "use of undeclared identifier", far from the real cause, and
only when building for the web -- so it survives any amount of native testing.
Write the body with no top-level comma instead:

    EM_ASM({
        var s = {};
        s.a = $0;
        s.b = $1;
        window.stats = s;
    }, x, y);

Detected by running the real preprocessor over Emscripten's own macro shape and
checking whether the stringified body still has balanced braces.

Usage: em_asm_check.py <file>...
"""

import os
import re
import subprocess
import sys
import tempfile

# Mirrors emscripten/em_asm.h's own definitions, reduced to the part that
# decides where the body ends, with markers so the stringified body can be
# recovered from the preprocessor's output.
MOCK_MACROS = r"""
#define CODE_EXPR(code) code
#define _EM_ASM_PREP_ARGS(...) , ##__VA_ARGS__
#define EM_ASM(code, ...) MARK_BEGIN CODE_EXPR(#code) MARK_END _EM_ASM_PREP_ARGS(__VA_ARGS__)
#define EM_ASM_INT(code, ...) MARK_BEGIN CODE_EXPR(#code) MARK_END _EM_ASM_PREP_ARGS(__VA_ARGS__)
#define EM_ASM_PTR(code, ...) MARK_BEGIN CODE_EXPR(#code) MARK_END _EM_ASM_PREP_ARGS(__VA_ARGS__)
#define EM_ASM_DOUBLE(code, ...) MARK_BEGIN CODE_EXPR(#code) MARK_END _EM_ASM_PREP_ARGS(__VA_ARGS__)
"""

CALL_RE = re.compile(r"\bEM_ASM(?:_INT|_PTR|_DOUBLE)?\s*\(")


def find_calls(source):
    """Yield (line_number, call_text) for each EM_ASM-family call in `source`."""
    for match in CALL_RE.finditer(source):
        depth = 0
        for index in range(match.end() - 1, len(source)):
            if source[index] == "(":
                depth += 1
            elif source[index] == ")":
                depth -= 1
                if depth == 0:
                    yield source.count("\n", 0, match.start()) + 1, source[match.start() : index + 1]
                    break


def stringified_body(call_text):
    """Return what the preprocessor actually passes to Emscripten as JS."""
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as handle:
        # `$0` is not a valid C token on its own; the substitution only has to
        # keep the text parseable, it is never compiled.
        handle.write(MOCK_MACROS + call_text.replace("$", "a") + "\n")
        temp_path = handle.name
    try:
        result = subprocess.run(["gcc", "-E", "-P", temp_path], capture_output=True, text=True)
    finally:
        os.unlink(temp_path)
    found = re.search(r"MARK_BEGIN(.*?)MARK_END", result.stdout, re.S)
    return found.group(1) if found else ""


def check_file(path):
    with open(path, encoding="utf-8") as handle:
        source = handle.read()

    failures = 0
    for line_number, call_text in find_calls(source):
        body = stringified_body(call_text)
        # A body that survived intact still has its braces balanced. One cut
        # short at a comma does not.
        if body.count("{") != body.count("}"):
            print(
                f"{path}:{line_number}: EM_ASM body is split by a top-level comma. "
                f"JavaScript only receives: {body.strip()[:100]}",
                file=sys.stderr,
            )
            failures += 1
    return failures


def main(paths):
    if not paths:
        return 0
    try:
        subprocess.run(["gcc", "--version"], capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        print("em_asm_check: gcc not available, skipping.", file=sys.stderr)
        return 0

    failures = sum(check_file(path) for path in paths)
    if failures:
        print(
            f"\nem_asm_check: {failures} EM_ASM body/bodies would be silently truncated. "
            "See the module docstring in misc/scripts/em_asm_check.py for the fix.",
            file=sys.stderr,
        )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
