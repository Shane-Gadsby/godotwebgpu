# Braced GDScript: language reference

Everything in the [GDScript reference](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_basics.html)
applies to `.gdb` files. This page lists only what is written differently. Each `.gdb` example compiles to exactly the
`.gd` code beside it, and the converter turns either into the other.

## Blocks

A block opens with `{` at the end of its header and closes with `}`. Indentation inside is free; the formatter sets it.

| `.gd` | `.gdb` |
| --- | --- |
| `func f(x: int) -> int:`<br>`⇥return x * 2` | `func f(x: int) -> int {`<br>`    return x * 2`<br>`}` |
| `if a:`<br>`⇥one()`<br>`elif b:`<br>`⇥two()`<br>`else:`<br>`⇥three()` | `if a {`<br>`    one()`<br>`} else if b {`<br>`    two()`<br>`} else {`<br>`    three()`<br>`}` |
| `for i in 3:`<br>`⇥print(i)` | `for i in 3 {`<br>`    print(i)`<br>`}` |
| `while busy():`<br>`⇥pass` | `while busy() {}` |
| `class Inner extends Node:`<br>`⇥var x := 1` | `class Inner extends Node {`<br>`    var x := 1`<br>`}` |
| `func f() -> void: print("hi")` | `func f() -> void { print("hi") }` |

- `elif` works too; `else if` is the same thing.
- `{}` is an empty block, so `pass` is only needed where you want to say so.
- A one-line block keeps its statements on one line: `if done { return }`. Several statements take `;`:
  `if hit { score += 1; flash() }`.
- `} else {` and `} else if c {` may also start on the line after the `}`.

### `match`

The value, then each branch's patterns, take a block:

```gdscript
match value {
    0 { return "zero" }
    1, 2 { return "one or two" }
    [var first, ..] { return "starts with %s" % first }
    { "kind": "dict" } { return "dictionary pattern" }
    var n when n is int and n > 100 { return "big" }
    _ {}
}
```

### Properties

A property's accessors go in a block after its declaration, in either form GDScript has:

```gdscript
var health: int = 100 {
    get { return health }
    set(value) {
        health = clampi(value, 0, max_health)
        health_changed.emit(health)
    }
}

var speed: float { get = _get_speed, set = _set_speed }
```

### Enums and dictionaries

Braces that are not blocks are dictionaries, enums and dictionary patterns, exactly as in `.gd`:

```gdscript
enum State { IDLE, RUN, JUMP }
enum { LEFT = -1, RIGHT = 1 }
var stats := { "hp": 10, "mp": 5 }
func defaults() -> Dictionary { return {} }
```

## When `{` opens a block

The tokenizer decides from the token before the `{`:

| Before the `{` | `{` opens | Examples |
| --- | --- | --- |
| Nothing on the line (statement start) | a dictionary | `{ "a": 1 } { ... }` as a match pattern |
| `enum`, or `enum Name` | an enum | `enum { A }`, `enum Dir { UP }` |
| `else` that starts a statement (`} else {`) | a block | |
| `else` inside an expression (`a if c else {}`) | a dictionary | |
| An identifier, literal, `)`, `]`, `}`, `self`, `true`, `false`, `null`, `void`, `_`, or another token that can end an expression | a block | `if ready {`, `-> void {`, `[1, 2] {`, `_ {`, `func(x) {` |
| Anything else: `=`, `:=`, `(`, `[`, `,`, `:`, `return`, an operator | a dictionary | `var d = {`, `f({`, `return {` |

So a dictionary after an operator is a dictionary even in a block header: `if d == {} {` compares with an empty
dictionary and then opens the block. The one place the rule needs help is an arrow lambda (below).

## Comments and documentation

| `.gd` | `.gdb` |
| --- | --- |
| `# note` | `// note` (`#` also works) |
| none | `/* several`<br>`   lines */` |
| `## Documents the next member.` | `/// Documents the next member.` (`##` also works) |
| `## Line one.`<br>`## Line two.` | `/**`<br>` * Line one.`<br>` * Line two.`<br>` */` |
| `#region Name` … `#endregion` | the same: `#region Name` … `#endregion` (the editor folds regions on its shortest line-comment marker) |

A `/** */` comment documents the member after it, like `##` lines. Leading ` * ` on each line is removed; other text,
including `@param`-style tags and BBCode such as `[param x]`, is kept as written and shows in the editor's help.

## JavaScript-style spellings

| `.gd` | `.gdb` | Notes |
| --- | --- | --- |
| `and`, `or`, `not` | `&&`, `\|\|`, `!` | Both spellings work in both file types. The formatter can normalize to either. |
| `a if c else b` | `c ? a : b` | Both work in `.gdb`. `?:` nests to the right: `x > 10 ? "big" : x > 5 ? "mid" : "small"`. |
| `func(x): return x * 2` | `(x) => x * 2` or `x => x * 2` | The body's value is returned. Parameters take types and defaults: `(a: int, b := 1) => a + b`. |
| `func(x):`<br>`⇥var y = x + 1`<br>`⇥return y` | `(x) => {`<br>`    var y = x + 1`<br>`    return y`<br>`}` | A block body returns only through `return`, as in a `func` lambda. |
| `func(): print("hi")` | `() => print("hi")` | An arrow whose body is a call to a function returning nothing compiles to a block body, so it is valid. |
| `func(x): return {"x": x}` | `(x) => ({"x": x})` | A `{` right after `=>` always opens a block body, so an arrow that returns a dictionary wraps it in parentheses. |

`func` lambdas work unchanged: `func(x) { return x * 2 }`, `func named(x) { ... }`.

## What stays the same

Newlines end statements; `;` separates statements; inside `( )`, `[ ]` and dictionaries, newlines are free; `\` at a
line end continues a statement. Annotations, `static`, `@onready`, `await`, signals, `class_name`, `extends`, typed
arrays and dictionaries, `super()`, `preload`, node paths (`$Node`, `%Unique`) and string literals of every kind are
unchanged.

Not available (and why) is listed in the [FAQ](faq.md#what-was-left-out).
