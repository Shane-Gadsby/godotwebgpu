# JavaScript (ES6+) for Godot (.gdj) — Assessment

Oct 18, 2026 · @Shane

## Question

Given the design choices in the Braced GDScript (`.gdb`) plan, should this project instead build a JavaScript ES6+ language for Godot as `.gdj` files, with a Rider plugin and full documentation? The constraint is no V8 or other JavaScript engine: either pure C++ bindings, or a translation layer that treats `.gdj` like GDScript (as the `.gdb` plan does) while staying 100% ES6+ compliant.

## Answer

No, not as described. "100% ES6+ compliant" and "no JavaScript engine" cannot both be met. Whatever executes ES6+ correctly *is* a JavaScript engine, whether it is called bindings, a translation layer or a compiler. The real choice is between three things, and two of them beat `.gdj`:

1. **GDScript semantics with JavaScript-style spelling.** This is the `.gdb` plan, which is honest about being GDScript and costs 17–21 weeks.
2. **Real JavaScript and TypeScript through an existing embedded engine.** Adopt [GodotJS](https://github.com/godotjs/GodotJS) (MIT; V8, QuickJS, quickjs-ng, JavaScriptCore, or the browser's own engine on web) rather than build anything. Rider's built-in TypeScript support then covers the whole IDE requirement with no plugin.
3. **Write a compliant engine yourself.** Measured in years, and it never finishes, because ECMAScript ships a new edition every June.

The middle path, JavaScript syntax lowered onto GDScript semantics, is the worst of the four options compared below. It is covered in detail because it is the most tempting.

## Why "100% compliant without an engine" is a contradiction

ES6+ compliance is measured by test262, about 53,000 tests. They test semantics, not syntax. The table lists what GDScript's VM does today against what the specification requires. Each row is something a translation layer cannot paper over: it needs new VM machinery.

| ECMAScript requires | GDScript today | What closing the gap means |
| --- | --- | --- |
| Exceptions: `throw` any value, `try`/`catch`/`finally` | No exceptions at all; errors stop the function | Unwinding in `gdscript_vm.cpp` across every call path. The engine is built without C++ exceptions, so a throw inside a signal callback must not cross native frames. That is a design problem, not an implementation detail. |
| Closures capture variables by reference | Lambdas capture locals **by value** when created | Heap-allocated environment records and new opcodes |
| Objects are property bags with prototype chains and property descriptors (`defineProperty`, getters/setters, `freeze`, enumeration order) | `Object` has a fixed class shape; `Dictionary` has no prototypes or descriptors | A second object model beside `Object`/`Variant` |
| Every number is an IEEE double: `5 / 2 === 2.5`, 32-bit bitwise operators, `2 ** 53` limits | `int` is 64-bit, `5 / 2 == 2` | A number type that is not `Variant::INT`, or emulation on every operation |
| Strings are UTF-16: `"😀".length === 2`, `charCodeAt` gives surrogates | `String` is UTF-32 (`char32_t`) | A separate string representation |
| Cycles are collected (parent ⇄ child, self-referencing closures) | `RefCounted` cycles leak by design | A tracing or cycle collector |
| `this` binding rules, `call`/`apply`/`bind`, lexical `this` in arrows | `self` is fixed per method | New call conventions |
| Generators, iterators, `async`/`await` with Promise job-queue ordering | `await` on signals and coroutines, with different ordering | Spec-exact microtask queue |
| `Proxy`, `Reflect`, `Symbol`, `WeakRef`, `FinalizationRegistry`, `BigInt`, typed arrays, ES-flavor `RegExp`, `Date`, `eval`/`Function`, modules, sloppy mode and Annex B | none | A standard library comparable in size to the language |
| Loose equality and coercion: `"1" == 1`, `[] + {}`, `typeof`, `undefined` vs `null` | type errors or different results | Coercion rules on every operator |

Once all of that exists, the GDScript VM has become a JavaScript engine that also runs GDScript. Every upstream change to `gdscript_vm.cpp` then conflicts with it, which breaks the porting requirement (R6) that shapes the whole `.gdb` design.

"Pure C++ bindings" does not get around this either. Bindings connect a language runtime to Godot's API, which is what GodotJS's C++ layer does. Something still has to run the JavaScript. Compiling JavaScript ahead of time to C++ or native code moves the problem without removing it:

- `eval`, `Proxy` and prototype mutation force a runtime into the output anyway.
- Every platform's C++ toolchain is needed at export time.
- Hot reload and live editing are lost.

## How long a from-scratch engine takes, by example

| Project | Approach | Where it stands |
| --- | --- | --- |
| QuickJS (Fabrice Bellard) | Interpreter in C | Upstream says it passes nearly 100% of test262 for ES2025 features, after years of work by one of the most productive systems programmers alive |
| Boa | Interpreter in Rust, community project begun in 2018–19 | About 87–90% of test262 at v0.20 (Dec 2024); a third-party report puts mid-2026 at about 96–99% depending on whether ignored tests count |
| Porffor | Ahead-of-time compiler, JS → Wasm/C, the closest thing to "no engine" | Self-described pre-alpha research project; reported passing roughly 14–35% of test262 |

A compliant `.gdj` is therefore a multi-year commitment for a team. That is before the Godot binding layer, the editor integration, the Rider plugin and the documentation the request includes.

## The tempting middle path: JavaScript syntax, GDScript semantics

This is the "translation layer that treats them like GDScript" option without the compliance requirement: parse ES6 syntax and lower it onto GDScript's AST, the way `.gdb` lowers braces. It can be built. But code that *looks* like JavaScript gets judged as JavaScript, so it fails in exactly the places JavaScript programmers trust it:

| Looks like | A JavaScript programmer expects | `.gdj` on GDScript semantics would do |
| --- | --- | --- |
| `let half = 5 / 2` | `2.5` | `2` |
| `let n = 0; const inc = () => n++; inc(); n` | `1` | `0` (by-value capture), if `++` were supported at all |
| `try { risky() } catch (e) { … }` | catches | must be rejected; GDScript has no exceptions |
| `"😀".length` | `2` | `1` |
| `if (x == "1")` with `x = 1` | `true` | `false` or a type error |
| `const o = { n: 1, get double() { return this.n * 2 } }` | an object with a getter | must be rejected or turned into a Dictionary with different behavior |
| `arr.map(x => x * 2)` on a Godot `Array` | works | works only through Godot's method names, signatures and semantics |
| `import _ from "lodash"` | the npm ecosystem | unavailable |

So JavaScript knowledge, Stack Overflow answers and LLM-written snippets all transfer just well enough to compile and then misbehave. `.gdb` avoids this by not pretending: the Tier A and Tier B spellings in its Phase 4 were admitted only because they mean exactly what they mean in JavaScript (D11).

Rough cost, if it were built anyway: 6–9 months for one developer.

- An ES2015+ parser with automatic semicolon insertion, regex-versus-division lexing, and the destructuring and arrow cover grammars.
- Lowering onto the GDScript AST, with a precise error for every construct that has no equivalent.
- A custom Rider plugin. Rider's own JavaScript support cannot be reused, because it would accept and complete code that `.gdj` rejects or runs differently.
- Documentation that spends most of its length on the differences.

Prettier could format it, which is the one piece that comes free.

## What each option buys, side by side

| | `.gdb` (current plan) | `.gdj`, JS syntax on GDScript semantics | `.gdj`, compliant, built here | GodotJS (adopt an existing engine) |
| --- | --- | --- | --- | --- |
| Language semantics | GDScript | GDScript dressed as JS | ECMAScript | ECMAScript (the engine's: near 100%) |
| test262 | not applicable | most of it fails | the goal, years away | the engine's result |
| npm and the JS ecosystem | no | no | in principle | yes |
| Effort, one developer | 17–21 weeks | ~6–9 months | multi-year | 1–2 week spike, then weeks |
| Rider: completion, inspections, formatting | Phase 11 plugin (4–6 weeks, inside the estimate) | custom plugin, larger | Rider's TS/JS support works, given typings | Rider's TS/JS support with GodotJS's generated `.d.ts` typings; Prettier and ESLint as usual |
| Debugging in Rider | DAP through the engine (Phase 11) | must be built | must be built | V8 inspector (Chrome DevTools protocol) on native; Safari with JavaScriptCore |
| Upstream porting cost | small, `// GDB:` hooks | medium: AST lowering tracks every GDScript AST change | severe if it lives in the GDScript VM | the module's maintainers' job; a third-party dependency |
| Fits this fork's web target | yes, nothing new on web | yes | yes, if it compiles to Wasm | browser backend uses the page's own JIT. Every Godot API call crosses the Wasm ↔ JS boundary, the cost this fork's `perf_guard` and `api_profile` work keeps fighting. Measure it. |
| Main risk | low–medium | users trusting JS intuition | never finishing | GodotJS is one small project (≈770 stars, last commit Jun 13, 2026, after a two-month gap) |

## Recommendation

Decide by what the JavaScript is *for*:

- **Brace syntax and JavaScript-style ergonomics for your own Godot code, keeping GDScript's editor integration, typing, `@export` and debugger:** go ahead with the `.gdb` plan as it stands. It already takes every JavaScript spelling that can be taken honestly.
- **Real JavaScript or TypeScript: npm packages, existing JS code, the full WebStorm-grade tooling Rider already ships:** do not build `.gdj`. Run a 1–2 week spike of GodotJS in this fork instead, with these gates:
  1. It builds as a module on `webgpu-4.8` next to the WebGPU driver, with `dev_mode=yes`
  2. It runs with quickjs-ng on native and the browser backend in a nothreads web export. Then try threads, the separate render thread and dlink, because the web build matrix is where a third-party module is most likely to break.
  3. A per-frame Godot API call budget measured with `perf_guard` / `api_profile`, against the same scene in GDScript
  4. Rider: completion, refactoring and debugging on a TypeScript project using the generated typings
  5. A judgement on maintenance: the module's code size, how it hooks into the engine, and what an upstream sync would cost

The two are not exclusive. `.gdb` and GodotJS could coexist in one project, because they touch different parts of the engine.

## Sources

- [GodotJS](https://github.com/godotjs/GodotJS): repository, README feature list, commit history (checked Oct 18, 2026)
- [godotjs/javascript](https://github.com/godotjs/javascript): the earlier QuickJS-based module
- [QuickJS](https://bellard.org/quickjs/): conformance and size claims
- [Boa v0.20 release](https://boajs.dev/blog/2024/12/05/boa-release-020); [third-party 2026 conformance report](https://www.issoh.co.jp/tech/details/3100/) (unverified)
- [Porffor](https://github.com/CanadaHonk/porffor); [JavaScript Weekly mention](https://javascriptweekly.com/link/158304/rss)
- GDScript facts (no exceptions, by-value lambda capture, 64-bit `int`, UTF-32 `String`, leaking `RefCounted` cycles): this tree's `modules/gdscript/` and `core/`, and the GDScript reference
