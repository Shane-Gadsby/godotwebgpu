# Web platform port

This folder contains the C++ and JavaScript code for the Web platform port,
compiled using [Emscripten](https://emscripten.org/).

It also contains a ESLint linting setup (see [`package.json`](package.json)).

See also [`misc/dist/html`](/misc/dist/html) folder for additional files used by
this platform such as the html shell (web page).

## Documentation

- [Compiling for the Web](https://docs.godotengine.org/en/latest/engine_details/development/compiling/compiling_for_web.html)
  - Instructions on building this platform port from source.
- [Exporting for the Web](https://docs.godotengine.org/en/latest/tutorials/export/exporting_for_web.html)
  - Instructions on using the compiled export templates to export a project.

## Console output (fork-specific)

Everything the engine writes to stderr reaches the browser as `console.error()`, because Emscripten
routes stderr through `Module.printErr` and that is what the shells pass it to. Warnings are the
exception: `WebLogger` in [`os_web.cpp`](os_web.cpp) intercepts `ERR_WARNING` and sends it through
`godot_js_os_print_warning()` to the page's `onPrintWarning` callback, which defaults to
`console.warn()`. Every other error type (`ERROR`, `SCRIPT ERROR`, `SHADER ERROR`) is untouched.

So a page has three hooks, all optional, all set on the `Engine` config:

| callback | receives | default |
|---|---|---|
| `onPrint` | stdout (`print()`, `print_verbose()`) | `console.log` |
| `onPrintWarning` | `WARN_PRINT` and friends, pre-formatted, one string | `console.warn` |
| `onPrintError` | stderr: every error type, plus anything else written there | `console.error` |

`print_verbose()` output needs the engine started with `--verbose`, which an exported page does by
adding it to `GODOT_CONFIG['args']` in the HTML.

## Artwork license

[`logo.svg`](export/logo.svg) and [`run_icon.svg`](export/run_icon.svg) are licensed under
[Creative Commons Attribution 3.0 Unported](https://www.w3.org/html/logo/faq.html#how-licenced)
per the HTML5 logo usage guidelines.
