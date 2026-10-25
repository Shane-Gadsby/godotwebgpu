#!/usr/bin/env bash
# Build wgsl_convert_cli — native host tool for build-time SPIR-V → WGSL conversion
# (our preprocessing passes, then SPIRV-Cross's WGSL backend, thirdparty/spirv-cross-wgsl).
#
# Usage:
#   ./drivers/webgpu/wgsl_cli/build.sh          # Build with auto-detected parallelism
#   ./drivers/webgpu/wgsl_cli/build.sh --clean   # Remove build artifacts and rebuild
#
# Output: bin/wgsl_convert_cli (bin/wgsl_convert_cli.exe on Windows), or OUTPUT if set.
#
# On Windows, run from Git Bash or MSYS2 with LLVM's clang++ (targets the MSVC
# ABI and finds the Visual Studio installation on its own). Set CXX to override.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
cd "$REPO_ROOT"

# Directories.
SPIRV_TOOLS_DIR="thirdparty/spirv-tools"
SPIRV_HEADERS_DIR="thirdparty/spirv-headers"
SHIM_DIR="drivers/webgpu/wgsl_cli"
SPIRV_CROSS_DIR="thirdparty/spirv-cross-wgsl"

# JOBS=<n> caps parallel compiles (several worktrees building at once share the cores and memory).
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) IS_WINDOWS=true ;;
    *) IS_WINDOWS=false ;;
esac

if [[ "$IS_WINDOWS" == true ]]; then
    if [[ -z "${CXX:-}" ]]; then
        if command -v clang++ >/dev/null 2>&1; then
            CXX=clang++
        elif [[ -x "/c/Program Files/LLVM/bin/clang++.exe" ]]; then
            CXX="/c/Program Files/LLVM/bin/clang++.exe"
        else
            echo "Error: clang++ not found. Install LLVM (https://github.com/llvm/llvm-project/releases) or set CXX." >&2
            exit 1
        fi
    fi
    EXE_SUFFIX=".exe"
else
    CXX="${CXX:-c++}"
    EXE_SUFFIX=""
fi
OUTPUT="${OUTPUT:-bin/wgsl_convert_cli$EXE_SUFFIX}"

# Per-OS object directory: a checkout shared between OSes (WSL, dual boot, a
# network drive) would otherwise reuse another OS's up-to-date-looking objects
# and fail to link.
BUILD_DIR="drivers/webgpu/wgsl_cli/.build/$(uname -s | cut -d_ -f1 | tr '[:upper:]' '[:lower:]')"
CLI_DIR="$BUILD_DIR/cli"

# Parse args.
CLEAN=false
for arg in "$@"; do
    [[ "$arg" == "--clean" ]] && CLEAN=true
done

if [[ "$CLEAN" == true ]]; then
    echo "Cleaning build artifacts..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR/spirv_tools" "$BUILD_DIR/spirv_cross" "$CLI_DIR"

# Common flags.
WARNINGS="-w"  # Suppress warnings from thirdparty code.
# CXXFLAGS/LDFLAGS from the environment are passed through, e.g.
# CXXFLAGS="-arch arm64 -arch x86_64" for a universal macOS binary.
COMMON_FLAGS="-O2 $WARNINGS ${CXXFLAGS:-}"

# Include paths for SPIRV-Tools.
SPIRV_TOOLS_INCLUDES=(
    -I"$SPIRV_TOOLS_DIR"
    -I"$SPIRV_TOOLS_DIR/source/"
    -I"$SPIRV_TOOLS_DIR/include/"
    -I"$SPIRV_TOOLS_DIR/generated/"
    -I"$SPIRV_HEADERS_DIR/include/"
    -I"$SPIRV_HEADERS_DIR/include/spirv/unified1/"
)

# SPIRV-Cross: its own spirv.hpp (newer than thirdparty/spirv-headers'), its own namespace, so a macOS editor that also links drivers/metal's
# copy (thirdparty/spirv-cross) gets no duplicate symbols; and exceptions, which
# are how SPIRV-Cross reports an unsupported shader (drivers/metal/SCsub does the same).
SPIRV_CROSS_FLAGS=(
    -I"$SPIRV_CROSS_DIR"
    -I"$SPIRV_CROSS_DIR/include"
    -DSPIRV_CROSS_NAMESPACE_OVERRIDE=spirv_cross_wgsl
    -fexceptions
)

# ─────────────────────────────────────────────────────────────────────────────
# Compile function: skip if .o is newer than source.
# ─────────────────────────────────────────────────────────────────────────────
compile_one() {
    local src="$1"
    local obj="$2"
    local std="$3"
    shift 3
    local flags=("$@")

    if [[ -f "$obj" && "$obj" -nt "$src" ]]; then
        return 0
    fi

    mkdir -p "$(dirname "$obj")"
    "$CXX" -c "$src" -o "$obj" -std="$std" $COMMON_FLAGS "${flags[@]}"
}

# ─────────────────────────────────────────────────────────────────────────────
# 1. Compile SPIRV-Tools
# ─────────────────────────────────────────────────────────────────────────────
echo "[1/4] Compiling SPIRV-Tools..."

SPIRV_TOOLS_OBJS=()
# Collect all .cpp files from the SCsub list (everything under source/).
while IFS= read -r src; do
    objname="${src#$SPIRV_TOOLS_DIR/}"
    objname="${objname%.cpp}.o"
    obj="$BUILD_DIR/spirv_tools/$objname"
    SPIRV_TOOLS_OBJS+=("$obj")
    # Run in parallel via background jobs.
    compile_one "$src" "$obj" "c++17" "${SPIRV_TOOLS_INCLUDES[@]}" &
    # Limit parallelism.
    if (( $(jobs -r | wc -l) >= JOBS )); then
        # `wait -n` (wait for the *next* job, keeping a rolling window) needs bash
        # >= 4.3. macOS's system bash is 3.2, where it fails instantly -- and with
        # the old `|| true` that failure was swallowed, silently disabling the
        # throttle and launching all ~570 compiles at once. Draining the current
        # wave instead is slower than a rolling window but correct everywhere.
        wait -n 2>/dev/null || wait
    fi
done < <(find "$SPIRV_TOOLS_DIR/source" -name '*.cpp' -not -name '*test*' -not -name '*_test.cpp' -not -path '*/test/*' | sort)
wait

echo "  ${#SPIRV_TOOLS_OBJS[@]} objects"

# ─────────────────────────────────────────────────────────────────────────────
# 2. Compile SPIRV-Cross (WGSL backend)
# ─────────────────────────────────────────────────────────────────────────────
SPIRV_CROSS_OBJS=()
# compile_one only compares an object with its .cpp. A SPIRV-Cross header edit (a local patch adding a member)
# left objects with two class layouts in one link, which crashes at run time (free(): invalid pointer), so any
# header newer than the stamp throws away everything compiled against them.
SPIRV_CROSS_STAMP="$BUILD_DIR/spirv_cross/.headers"
if [[ ! -f "$SPIRV_CROSS_STAMP" ]] || [[ -n "$(find "$SPIRV_CROSS_DIR" drivers/webgpu/spirv_cross_wrapper.h \( -name '*.hpp' -o -name '*.h' \) -newer "$SPIRV_CROSS_STAMP" -print -quit)" ]]; then
    find "$BUILD_DIR/spirv_cross" "$CLI_DIR" -name '*.o' -delete
    touch "$SPIRV_CROSS_STAMP"
fi
echo "[2/4] Compiling SPIRV-Cross (WGSL backend)..."
for name in spirv_cfg spirv_cross spirv_cross_parsed_ir spirv_cross_util spirv_glsl spirv_parser spirv_wgsl; do
    obj="$BUILD_DIR/spirv_cross/$name.o"
    SPIRV_CROSS_OBJS+=("$obj")
    compile_one "$SPIRV_CROSS_DIR/$name.cpp" "$obj" "c++17" "${SPIRV_CROSS_FLAGS[@]}" &
done
wait
echo "  ${#SPIRV_CROSS_OBJS[@]} objects"

# ─────────────────────────────────────────────────────────────────────────────
# 3. Compile CLI sources (spirv_preprocess.cpp + spirv_cross_wrapper.cpp + main.cpp)
# ─────────────────────────────────────────────────────────────────────────────
echo "[3/4] Compiling CLI sources..."

# spirv_preprocess.cpp — compiled with shim include path (before repo root so
# the shim core/templates/ is found instead of Godot's).
compile_one "drivers/webgpu/spirv_preprocess.cpp" \
    "$CLI_DIR/spirv_preprocess.o" \
    "c++17" \
    -I"$SHIM_DIR" \
    "${SPIRV_TOOLS_INCLUDES[@]}" &

# spirv_opt_passes.cpp — the passes written against SPIRV-Tools' optimizer IR;
# needs SPIRV-Tools' internal headers, but not the shim (no Godot types).
compile_one "drivers/webgpu/spirv_opt_passes.cpp" \
    "$CLI_DIR/spirv_opt_passes.o" \
    "c++17" \
    "${SPIRV_TOOLS_INCLUDES[@]}" &

# spirv_lite_reflect.cpp — the SPIRV-Tools-free half of the reflection, which
# spirv_preprocess.cpp delegates has_spec_constants() to. Same shim include
# path, no SPIRV-Tools includes needed.
compile_one "drivers/webgpu/spirv_lite_reflect.cpp" \
    "$CLI_DIR/spirv_lite_reflect.o" \
    "c++17" \
    -I"$SHIM_DIR" &

CROSS_FLAGS=(-I"drivers/webgpu/" "${SPIRV_CROSS_FLAGS[@]}")
compile_one "drivers/webgpu/spirv_cross_wrapper.cpp" \
    "$CLI_DIR/spirv_cross_wrapper.o" \
    "c++17" \
    "${CROSS_FLAGS[@]}" &

# main.cpp — compiled with the shim, SPIRV-Tools and SPIRV-Cross includes.
compile_one "drivers/webgpu/wgsl_cli/main.cpp" \
    "$CLI_DIR/main.o" \
    "c++17" \
    -I"$SHIM_DIR" \
    -I"drivers/webgpu/" \
    "${SPIRV_TOOLS_INCLUDES[@]}" "${CROSS_FLAGS[@]}" &

wait

# ─────────────────────────────────────────────────────────────────────────────
# 4. Link
# ─────────────────────────────────────────────────────────────────────────────
echo "[4/4] Linking wgsl_convert_cli..."

# Filter to only .o files that were successfully compiled.
LINK_OBJS=("$CLI_DIR/main.o" "$CLI_DIR/spirv_preprocess.o" "$CLI_DIR/spirv_opt_passes.o" "$CLI_DIR/spirv_lite_reflect.o" "$CLI_DIR/spirv_cross_wrapper.o")
for obj in "${SPIRV_TOOLS_OBJS[@]}" "${SPIRV_CROSS_OBJS[@]}"; do
    [[ -f "$obj" ]] && LINK_OBJS+=("$obj")
done

echo "  Linking ${#LINK_OBJS[@]} objects..."

mkdir -p bin
# Pass the object list through a response file: a couple of thousand paths
# overflow Windows' 32K command-line limit (and are harmless elsewhere).
LINK_RSP="$CLI_DIR/link.rsp"
printf '"%s"
' "${LINK_OBJS[@]}" >"$LINK_RSP"
"$CXX" -o "$OUTPUT" "@$LINK_RSP" -O2 ${CXXFLAGS:-} ${LDFLAGS:-}

echo ""
echo "Built: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
