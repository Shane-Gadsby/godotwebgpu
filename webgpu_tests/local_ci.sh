#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# local_ci.sh — Run the full WebGPU test suite locally (mirrors CI + Safari)
#
# Usage:
#   ./webgpu_tests/local_ci.sh              # Rebuild engine + re-export + run all tests
#   ./webgpu_tests/local_ci.sh --no-rebuild # Run all tests against the existing exports
#   ./webgpu_tests/local_ci.sh --no-safari  # Skip Safari (faster, no AppleScript needed)
#   ./webgpu_tests/local_ci.sh --quick      # Shader corpus + scene smoketest only (no rebuild)
#   ./webgpu_tests/local_ci.sh --quick --rebuild  # Rebuild + quick tests only
#   ./webgpu_tests/local_ci.sh --no-export  # Rebuild but keep the existing exports
#   ./webgpu_tests/local_ci.sh --export     # Re-export without rebuilding
#   ./webgpu_tests/local_ci.sh --no-dlink   # Skip the dlink template compile check
#   ./webgpu_tests/local_ci.sh --dev-mode   # Build with dev_mode=yes, as CI does
#                                           # (warnings=extra werror=yes)
#
# What the rebuild builds, and why it matters:
#   The editor, the dlink web template, and the **non-dlink, nothreads** one --
#   that last being the exact pair, with the editor, that the scene smoketest
#   exports with. This used to build *only* the dlink template, which is a
#   different file (…nothreads.dlink.zip) that no later stage ever loaded, so a
#   green run said nothing about the engine in the working tree. The run now
#   re-exports every scene from what it just built, so it does.
#
#   The dlink build is kept as a compile check -- nothing loads it, but dropping
#   it would let a dlink-only build break through unnoticed. --no-dlink skips it.
#
#   The two builds have to happen in this order, and the web one has to come last:
#   interleaving them in one tree goes stale in register_module_types.gen and ships
#   a template that dies in callMain() (webgpu_notes/TASKS.md Task 40). The stale
#   objects are removed below rather than hoped about.
#
# Prerequisites:
#   - Node.js 20+
#   - glslangValidator (brew install glslang)
#   - Playwright browsers: npx playwright install chromium firefox
#   - Emscripten with emdawnwebgpu, for the web template build. The script sources
#     $EMSDK_ENV (default ~/emsdk/emsdk_env.sh) when emcc is not already on PATH.
#   - Safari: Enable Develop → "Allow JavaScript from Apple Events" (macOS only)
#
# Exit codes:
#   0 = all tests pass
#   1 = one or more tests failed
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Defaults
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Parse args
NO_SAFARI=false
QUICK=false
DO_DLINK=true
DEV_MODE=false
REBUILD_EXPLICIT=""  # "", "yes", or "no"
EXPORT_EXPLICIT=""   # "", "yes", or "no"
for arg in "$@"; do
    [[ "$arg" == "--no-safari" ]] && NO_SAFARI=true
    [[ "$arg" == "--quick" ]] && QUICK=true
    [[ "$arg" == "--rebuild" ]] && REBUILD_EXPLICIT="yes"
    [[ "$arg" == "--no-rebuild" ]] && REBUILD_EXPLICIT="no"
    [[ "$arg" == "--export" ]] && EXPORT_EXPLICIT="yes"
    [[ "$arg" == "--no-export" ]] && EXPORT_EXPLICIT="no"
    [[ "$arg" == "--no-dlink" ]] && DO_DLINK=false
    [[ "$arg" == "--dev-mode" ]] && DEV_MODE=true
done

# dev_mode=yes is what CI builds with (webgpu_tests.yml's SCONS_FLAGS), and in
# Godot it implies warnings=extra werror=yes. Nothing local used it, which is how
# an unused-variable error sat in drivers/webgpu/spirv_preprocess.cpp failing
# every CI run for two days while every tier here stayed green. Off by default
# because turning it on recompiles every object with different flags -- so it is
# a deliberate "check what CI will say", not the normal path.
# A scalar, not an array: macOS still ships bash 3.2, where expanding an empty
# array under `set -u` is an unbound-variable error, and this script runs there
# for the Safari tier. Unquoted expansion of an empty scalar contributes no
# argument at all, and "dev_mode=yes" has no whitespace to split on.
DEV_MODE_FLAG=""
DEV_MODE_NOTE=""
if [[ "$DEV_MODE" == true ]]; then
    DEV_MODE_FLAG="dev_mode=yes"
    DEV_MODE_NOTE=" [dev_mode]"
fi

# Rebuild logic: full mode rebuilds by default, --quick does not.
# --rebuild/--no-rebuild always override.
if [[ "$REBUILD_EXPLICIT" == "yes" ]]; then
    DO_REBUILD=true
elif [[ "$REBUILD_EXPLICIT" == "no" ]]; then
    DO_REBUILD=false
elif [[ "$QUICK" == true ]]; then
    DO_REBUILD=false
else
    DO_REBUILD=true
fi

# Export follows the rebuild unless asked otherwise: a fresh engine that is not
# re-exported is not actually under test, and exports made by a differently
# versioned engine are worse than useless (the shader cache is keyed to the git
# commit, so they load with {baked: 0} and a ~5x slower start -- Task 36).
if [[ "$EXPORT_EXPLICIT" == "yes" ]]; then
    DO_EXPORT=true
elif [[ "$EXPORT_EXPLICIT" == "no" ]]; then
    DO_EXPORT=false
else
    DO_EXPORT=$DO_REBUILD
fi

# Which editor binary the export runs, and which scons platform builds it.
case "$(uname -s)" in
    Darwin)
        SCONS_PLATFORM="macos"
        EDITOR_BIN="$REPO_ROOT/bin/godot.macos.editor.$(uname -m)"
        ;;
    Linux)
        SCONS_PLATFORM="linuxbsd"
        EDITOR_BIN="$REPO_ROOT/bin/godot.linuxbsd.editor.x86_64"
        ;;
    *)
        SCONS_PLATFORM="windows"
        EDITOR_BIN="$REPO_ROOT/bin/godot.windows.editor.x86_64.exe"
        ;;
esac
# scenes.json hardcodes a macOS arm64 editor path, so the override is what makes
# this work anywhere else. The template default there is already the non-dlink
# nothreads zip, but name it anyway so the two can never drift apart.
TEMPLATE_ZIP="$REPO_ROOT/bin/godot.web.template_release.wasm32.nothreads.zip"
export GODOT_EDITOR_BIN="$EDITOR_BIN"
export GODOT_TEMPLATE_ZIP="$TEMPLATE_ZIP"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m'

PASSED=0
FAILED=0
SKIPPED=0
RESULTS=()

run_test() {
    local name="$1"
    local dir="$2"
    shift 2
    local cmd=("$@")

    printf "${BOLD}▶ %-40s${NC}" "$name"

    if [[ ! -d "$dir" ]]; then
        printf "${YELLOW}SKIP${NC} (directory not found)\n"
        SKIPPED=$((SKIPPED + 1))
        RESULTS+=("SKIP  $name")
        return
    fi

    local output
    if output=$(cd "$dir" && "${cmd[@]}" 2>&1); then
        printf "${GREEN}PASS${NC}\n"
        PASSED=$((PASSED + 1))
        RESULTS+=("PASS  $name")
    else
        printf "${RED}FAIL${NC}\n"
        # Show last 10 lines of output on failure
        echo "$output" | tail -10 | sed 's/^/    /'
        FAILED=$((FAILED + 1))
        RESULTS+=("FAIL  $name")
    fi
}

# ──────────────────────────────────────────────────────────────────────────────
# Ensure playwright is installed where needed
# ──────────────────────────────────────────────────────────────────────────────
ensure_playwright() {
    local dir="$1"
    if [[ ! -d "$dir/node_modules/playwright" ]]; then
        echo "  [setup] Installing playwright in $dir..."
        (cd "$dir" && [[ ! -f package.json ]] && npm init -y > /dev/null 2>&1; npm install playwright > /dev/null 2>&1)
    fi
}

# ──────────────────────────────────────────────────────────────────────────────
echo ""
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║   WebGPU Local CI — Full Test Suite                      ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

if [[ "$QUICK" == true ]]; then
    echo "  Mode: --quick (shader corpus + scene smoketest only)"
else
    echo "  Mode: full"
fi
if [[ "$DO_REBUILD" == true ]]; then
    if [[ "$DO_DLINK" == true ]]; then
        echo "  Rebuild: yes (-j$JOBS) — editor + dlink and non-dlink web templates"
    else
        echo "  Rebuild: yes (-j$JOBS) — editor + non-dlink web template (--no-dlink)"
    fi
else
    echo "  Rebuild: no"
fi
if [[ "$DEV_MODE" == true ]]; then
    echo "  dev_mode: yes — warnings=extra werror=yes, as CI builds"
fi
if [[ "$DO_EXPORT" == true ]]; then
    echo "  Re-export: yes — all scenes, from the binaries above"
else
    echo "  Re-export: no — testing the existing scene_smoketest/exports/"
fi
if [[ "$NO_SAFARI" == true ]]; then
    echo "  Safari: skipped (--no-safari)"
fi
echo ""

# ──────────────────────────────────────────────────────────────────────────────
# 0. Engine Rebuild
# ──────────────────────────────────────────────────────────────────────────────
# scons, run from the repo root. A function rather than `env -C`, which is a GNU
# coreutils extension that BSD env (macOS) does not have.
scons_in_repo() {
    ( cd "$REPO_ROOT" && scons "$@" )
}

build_step() {
    local name="$1"
    shift
    printf "${BOLD}▶ %-40s${NC}" "$name"
    if output=$("$@" 2>&1); then
        printf "${GREEN}PASS${NC}\n"
        PASSED=$((PASSED + 1))
        RESULTS+=("PASS  $name")
    else
        printf "${RED}FAIL${NC}\n"
        echo "$output" | tail -20 | sed 's/^/    /'
        FAILED=$((FAILED + 1))
        RESULTS+=("FAIL  $name")
        echo ""
        echo "Build failed — aborting."
        exit 1
    fi
}

if [[ "$DO_REBUILD" == true ]]; then
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Stage 0: Engine Rebuild (editor + web templates)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""

    # webgpu=yes on the editor is what enables the export-time WGSL baker; without
    # it the export ships unbaked shaders and loads ~5x slower.
    build_step "scons editor ($SCONS_PLATFORM)$DEV_MODE_NOTE" \
        scons_in_repo platform="$SCONS_PLATFORM" target=editor webgpu=yes $DEV_MODE_FLAG -j"$JOBS"

    # Task 40: an editor build leaves the web target's register_module_types.gen
    # object stale, and the resulting template dies in callMain() with
    # "resolved is not a function". Clear it per variant, right before that
    # variant is built -- the dlink and non-dlink objects have separate names, so
    # each needs its own.
    clear_stale_module_objects() {
        local variant="$1" # e.g. wasm32.nothreads or wasm32.nothreads.dlink
        rm -f "$REPO_ROOT/bin/obj/modules/register_module_types.gen.web.template_release.$variant.o" \
              "$REPO_ROOT/bin/obj/modules/libmodules.web.template_release.$variant.a"
    }

    # emcc is only needed from here on, so check for it after the editor is built.
    if ! command -v emcc >/dev/null 2>&1; then
        EMSDK_ENV="${EMSDK_ENV:-$HOME/emsdk/emsdk_env.sh}"
        if [[ -f "$EMSDK_ENV" ]]; then
            printf "${BOLD}▶ %-40s${NC}" "source $(basename "$EMSDK_ENV")"
            # shellcheck disable=SC1090
            source "$EMSDK_ENV" >/dev/null 2>&1 || true
            if command -v emcc >/dev/null 2>&1; then
                printf "${GREEN}PASS${NC}\n"
            else
                printf "${RED}FAIL${NC}\n"
                echo "    Sourced $EMSDK_ENV but emcc is still not on PATH."
                exit 1
            fi
        else
            echo "  emcc is not on PATH and $EMSDK_ENV does not exist."
            echo "  Install Emscripten, or point EMSDK_ENV at your emsdk_env.sh."
            exit 1
        fi
    fi

    # The dlink (GDExtension-support) template. Nothing downstream loads it -- the
    # smoketest exports with the non-dlink one -- so this is a compile check, not
    # a tested artifact. It is built first so the non-dlink template stays the
    # last thing written before the export, and skipped by --no-dlink when the
    # extra ~1 minute is not wanted.
    if [[ "$DO_DLINK" == true ]]; then
        clear_stale_module_objects "wasm32.nothreads.dlink"
        build_step "scons web template (dlink, nothreads)$DEV_MODE_NOTE" \
            scons_in_repo platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no $DEV_MODE_FLAG -j"$JOBS"
    else
        printf "${BOLD}▶ %-40s${NC}${YELLOW}SKIP${NC} (--no-dlink)\n" "scons web template (dlink, nothreads)"
        SKIPPED=$((SKIPPED + 1))
        RESULTS+=("SKIP  scons web template (dlink, nothreads)")
    fi

    # The non-dlink nothreads template: the one the scene smoketest exports with,
    # and therefore the one everything after Stage 0 is actually testing. Built
    # last, so it is the freshest thing on disk when the export runs.
    clear_stale_module_objects "wasm32.nothreads"
    build_step "scons web template (non-dlink, nothreads)$DEV_MODE_NOTE" \
        scons_in_repo platform=web target=template_release webgpu=yes opengl3=no threads=no $DEV_MODE_FLAG -j"$JOBS"

    echo ""
fi

# ──────────────────────────────────────────────────────────────────────────────
# 1. Shader Corpus
# ──────────────────────────────────────────────────────────────────────────────
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Stage 1: Shader Corpus (SPIR-V → WGSL)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

run_test "Compile GLSL fixtures" \
    "$SCRIPT_DIR/shader_corpus" \
    bash ./compile_fixtures.sh

run_test "SPIR-V → WGSL validation" \
    "$SCRIPT_DIR/shader_corpus" \
    node run_tests.mjs

echo ""

# ──────────────────────────────────────────────────────────────────────────────
# 2. Unit Tests (driver)
# ──────────────────────────────────────────────────────────────────────────────
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Stage 2: Unit Tests"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

run_test "Driver unit tests (332)" \
    "$SCRIPT_DIR/driver_unit_tests" \
    node run_tests.mjs

run_test "SPIR-V preprocessing tests (205)" \
    "$SCRIPT_DIR/preprocessing_tests" \
    node run_tests.mjs

run_test "Specialization-constant override tests" \
    "$SCRIPT_DIR/spec_constant_overrides" \
    node run_tests.mjs

run_test "WGSL precompile Python tests" \
    "$SCRIPT_DIR/wgsl_cache" \
    python3 test_wgsl_precompile.py

run_test "WGSL precompile JS tests" \
    "$SCRIPT_DIR/wgsl_cache" \
    node test_wgsl_cache.mjs

echo ""

# ──────────────────────────────────────────────────────────────────────────────
# 3. Scene Smoketest (multi-browser)
# ──────────────────────────────────────────────────────────────────────────────
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Stage 3: Scene Smoketest (19 scenes x browsers)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

if [[ "$DO_EXPORT" == true ]]; then
    if [[ ! -x "$EDITOR_BIN" ]]; then
        printf "${BOLD}▶ %-40s${NC}${RED}FAIL${NC}\n" "Re-export scenes"
        echo "    No editor at $EDITOR_BIN — build one, or pass --no-export."
        FAILED=$((FAILED + 1))
        RESULTS+=("FAIL  Re-export scenes")
        exit 1
    fi
    if [[ ! -f "$TEMPLATE_ZIP" ]]; then
        printf "${BOLD}▶ %-40s${NC}${RED}FAIL${NC}\n" "Re-export scenes"
        echo "    No template at $TEMPLATE_ZIP — build one, or pass --no-export."
        FAILED=$((FAILED + 1))
        RESULTS+=("FAIL  Re-export scenes")
        exit 1
    fi
    # Export once, here, rather than once per browser: the browser stages then all
    # run against the same exports, which is also what makes a cross-browser
    # difference mean something.
    #
    # A failure here is fatal rather than counted, because every stage after it
    # would silently be testing the *previous* exports -- which is the failure
    # mode this whole change exists to remove.
    run_test "Re-export scenes (editor + template above)" \
        "$SCRIPT_DIR/scene_smoketest" \
        node run_scenes.mjs --export-only
    if [[ $FAILED -gt 0 ]]; then
        echo ""
        echo "Export failed — aborting rather than testing stale exports."
        exit 1
    fi
else
    printf "${BOLD}▶ %-40s${NC}${YELLOW}SKIP${NC} (--no-export / no rebuild)\n" "Re-export scenes"
    SKIPPED=$((SKIPPED + 1))
    RESULTS+=("SKIP  Re-export scenes")
    echo "    Testing whatever is already in scene_smoketest/exports/."
fi

run_test "Scene smoketest — Chrome (19 scenes)" \
    "$SCRIPT_DIR/scene_smoketest" \
    node run_scenes.mjs --browser chrome --timeout 30000

run_test "Scene smoketest — Firefox (19 scenes)" \
    "$SCRIPT_DIR/scene_smoketest" \
    node run_scenes.mjs --browser firefox --timeout 30000

if [[ "$NO_SAFARI" == false && "$(uname)" == "Darwin" ]]; then
    run_test "Scene smoketest — Safari (19 scenes)" \
        "$SCRIPT_DIR/scene_smoketest" \
        node run_scenes.mjs --browser safari --timeout 30000
else
    printf "${BOLD}▶ %-40s${NC}${YELLOW}SKIP${NC} (--no-safari or not macOS)\n" "Scene smoketest — Safari (19 scenes)"
    SKIPPED=$((SKIPPED + 1))
    RESULTS+=("SKIP  Scene smoketest — Safari (19 scenes)")
fi

echo ""

if [[ "$QUICK" == true ]]; then
    # Skip remaining stages in quick mode
    echo "(--quick: skipping resource lifecycle and screenshot comparison)"
    echo ""
else

# ──────────────────────────────────────────────────────────────────────────────
# 4. Resource Lifecycle
# ──────────────────────────────────────────────────────────────────────────────
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Stage 4: Resource Lifecycle Stress Test"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

ensure_playwright "$SCRIPT_DIR/resource_lifecycle"

run_test "Resource lifecycle stress tests" \
    "$SCRIPT_DIR/resource_lifecycle" \
    node run_tests.mjs

echo ""

# ──────────────────────────────────────────────────────────────────────────────
# 5. Screenshot Comparison
# ──────────────────────────────────────────────────────────────────────────────
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Stage 5: Screenshot Comparison (Chrome + Firefox)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

ensure_playwright "$SCRIPT_DIR/screenshot_comparison"

run_test "Screenshot comparison" \
    "$SCRIPT_DIR/screenshot_comparison" \
    node screenshot_tests.mjs

echo ""

fi  # end of non-quick block

# ──────────────────────────────────────────────────────────────────────────────
# Summary
# ──────────────────────────────────────────────────────────────────────────────
echo "═══════════════════════════════════════════════════════════════"
echo "  RESULTS"
echo "═══════════════════════════════════════════════════════════════"
echo ""
for r in "${RESULTS[@]}"; do
    case "$r" in
        PASS*) printf "  ${GREEN}✓${NC} %s\n" "${r#PASS  }" ;;
        FAIL*) printf "  ${RED}✗${NC} %s\n" "${r#FAIL  }" ;;
        SKIP*) printf "  ${YELLOW}○${NC} %s\n" "${r#SKIP  }" ;;
    esac
done
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
printf "  Total: ${GREEN}%d passed${NC}, ${RED}%d failed${NC}, ${YELLOW}%d skipped${NC}\n" "$PASSED" "$FAILED" "$SKIPPED"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

if [[ $FAILED -gt 0 ]]; then
    exit 1
fi
