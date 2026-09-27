#!/usr/bin/env bash
# Build tint_convert_cli — native host tool for build-time SPIR-V → WGSL conversion.
#
# Usage:
#   ./drivers/webgpu/tint_cli/build.sh          # Build with auto-detected parallelism
#   ./drivers/webgpu/tint_cli/build.sh --clean   # Remove build artifacts and rebuild
#
# Output: bin/tint_convert_cli (bin/tint_convert_cli.exe on Windows)
#
# On Windows, run from Git Bash or MSYS2 with LLVM's clang++ (targets the MSVC
# ABI and finds the Visual Studio installation on its own). Set CXX to override.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
cd "$REPO_ROOT"

# Directories.
TINT_DIR="thirdparty/tint"
SPIRV_TOOLS_DIR="thirdparty/spirv-tools"
SPIRV_HEADERS_DIR="thirdparty/spirv-headers"
SHIM_DIR="drivers/webgpu/tint_cli"

JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

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
OUTPUT="bin/tint_convert_cli$EXE_SUFFIX"

# Per-OS object directory: a checkout shared between OSes (WSL, dual boot, a
# network drive) would otherwise reuse another OS's up-to-date-looking objects
# and fail to link.
BUILD_DIR="drivers/webgpu/tint_cli/.build/$(uname -s | cut -d_ -f1 | tr '[:upper:]' '[:lower:]')"

# Parse args.
CLEAN=false
for arg in "$@"; do
    [[ "$arg" == "--clean" ]] && CLEAN=true
done

if [[ "$CLEAN" == true ]]; then
    echo "Cleaning build artifacts..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR/spirv_tools" "$BUILD_DIR/tint" "$BUILD_DIR/cli"

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

# Include paths for Tint.
TINT_INCLUDES=(
    -I"$TINT_DIR"
    -I"$TINT_DIR/src/"
    -I"$SPIRV_TOOLS_DIR"
    -I"$SPIRV_TOOLS_DIR/source/"
    -I"$SPIRV_TOOLS_DIR/include/"
    -I"$SPIRV_TOOLS_DIR/generated/"
    -I"$SPIRV_HEADERS_DIR/include/"
    -I"$SPIRV_HEADERS_DIR/include/spirv/unified1/"
)

# Tint preprocessor defines (must match SCsub).
TINT_DEFINES=(
    -DTINT_BUILD_SPV_READER=1
    -DTINT_BUILD_WGSL_WRITER=1
    -DTINT_BUILD_WGSL_READER=0
    -DTINT_BUILD_SPV_WRITER=0
    -DTINT_BUILD_GLSL_WRITER=0
    -DTINT_BUILD_HLSL_WRITER=0
    -DTINT_BUILD_MSL_WRITER=0
    -DTINT_BUILD_NULL_WRITER=0
    -DTINT_BUILD_SYNTAX_TREE_WRITER=0
    -DTINT_BUILD_IR_BINARY=0
)

# Keep <windows.h> (pulled in by Tint's *_windows.cc sources) from defining
# min()/max() macros that break std::min/std::max in everything else.
if [[ "$IS_WINDOWS" == true ]]; then
    TINT_DEFINES+=(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)
fi

# Detect platform-specific Tint sources.
case "$(uname -s)" in
    Darwin)
        TINT_PLATFORM_SOURCES=(
            "src/tint/utils/command/command_posix.cc"
            "src/tint/utils/file/tmpfile_posix.cc"
            "src/tint/utils/system/env_other.cc"
            "src/tint/utils/system/executable_file_mac.cc"
            "src/tint/utils/system/terminal_posix.cc"
            "src/tint/utils/text/styled_text_printer_posix.cc"
        )
        ;;
    Linux)
        TINT_PLATFORM_SOURCES=(
            "src/tint/utils/command/command_posix.cc"
            "src/tint/utils/file/tmpfile_posix.cc"
            "src/tint/utils/system/env_other.cc"
            "src/tint/utils/system/executable_path_linux.cc"
            "src/tint/utils/system/terminal_posix.cc"
            "src/tint/utils/text/styled_text_printer_posix.cc"
        )
        ;;
    MINGW* | MSYS* | CYGWIN*)
        TINT_PLATFORM_SOURCES=(
            "src/tint/utils/command/command_windows.cc"
            "src/tint/utils/file/tmpfile_windows.cc"
            "src/tint/utils/system/env_windows.cc"
            "src/tint/utils/system/executable_path_windows.cc"
            "src/tint/utils/system/terminal_windows.cc"
            "src/tint/utils/text/styled_text_printer_windows.cc"
        )
        ;;
    *)
        TINT_PLATFORM_SOURCES=(
            "src/tint/utils/command/command_other.cc"
            "src/tint/utils/file/tmpfile_other.cc"
            "src/tint/utils/system/env_other.cc"
            "src/tint/utils/system/terminal_other.cc"
            "src/tint/utils/text/styled_text_printer_other.cc"
        )
        ;;
esac

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
        wait -n 2>/dev/null || true
    fi
done < <(find "$SPIRV_TOOLS_DIR/source" -name '*.cpp' -not -name '*test*' -not -name '*_test.cpp' -not -path '*/test/*' | sort)
wait

echo "  ${#SPIRV_TOOLS_OBJS[@]} objects"

# ─────────────────────────────────────────────────────────────────────────────
# 2. Compile Tint
# ─────────────────────────────────────────────────────────────────────────────
echo "[2/4] Compiling Tint..."

# Collect Tint source files from the SCsub list.
# Rather than duplicating the full 400-line list, find all .cc files that match
# the SCsub pattern (excluding test files, benchmarks, fuzzers).
TINT_OBJS=()
while IFS= read -r src; do
    objname="${src#$TINT_DIR/}"
    objname="${objname%.cc}.o"
    obj="$BUILD_DIR/tint/$objname"
    TINT_OBJS+=("$obj")
    compile_one "$src" "$obj" "c++20" "${TINT_INCLUDES[@]}" "${TINT_DEFINES[@]}" &
    if (( $(jobs -r | wc -l) >= JOBS )); then
        wait -n 2>/dev/null || true
    fi
done < <(find "$TINT_DIR/src/tint" -name '*.cc' \
    -not -name '*_test.cc' \
    -not -name '*_bench*.cc' \
    -not -name '*_fuzz*.cc' \
    -not -path '*/test/*' \
    -not -path '*/bench/*' \
    -not -path '*/fuzz/*' \
    -not -path '*/cmd/*' \
    -not -name 'main.cc' \
    -not -name 'decode.cc' \
    -not -name 'encode.cc' \
    -not -name 'command_posix.cc' \
    -not -name 'command_windows.cc' \
    -not -name 'command_other.cc' \
    -not -name 'tmpfile_posix.cc' \
    -not -name 'tmpfile_windows.cc' \
    -not -name 'tmpfile_other.cc' \
    -not -name 'env_other.cc' \
    -not -name 'env_windows.cc' \
    -not -name 'executable_file_mac.cc' \
    -not -name 'executable_path_linux.cc' \
    -not -name 'executable_path_windows.cc' \
    -not -name 'terminal_posix.cc' \
    -not -name 'terminal_windows.cc' \
    -not -name 'terminal_other.cc' \
    -not -name 'styled_text_printer_posix.cc' \
    -not -name 'styled_text_printer_windows.cc' \
    -not -name 'styled_text_printer_other.cc' \
    -not -name 'args.cc' \
    -not -name 'cli.cc' \
    | sort)

# Add platform-specific sources.
for src in "${TINT_PLATFORM_SOURCES[@]}"; do
    full_src="$TINT_DIR/$src"
    if [[ -f "$full_src" ]]; then
        objname="${src%.cc}.o"
        obj="$BUILD_DIR/tint/$objname"
        TINT_OBJS+=("$obj")
        compile_one "$full_src" "$obj" "c++20" "${TINT_INCLUDES[@]}" "${TINT_DEFINES[@]}" &
    fi
done

# CLI/args utility needed by Tint internals.
for src in "src/tint/utils/command/args.cc" "src/tint/utils/command/cli.cc"; do
    full_src="$TINT_DIR/$src"
    if [[ -f "$full_src" ]]; then
        objname="${src%.cc}.o"
        obj="$BUILD_DIR/tint/$objname"
        TINT_OBJS+=("$obj")
        compile_one "$full_src" "$obj" "c++20" "${TINT_INCLUDES[@]}" "${TINT_DEFINES[@]}" &
    fi
done

wait
echo "  ${#TINT_OBJS[@]} objects"

# ─────────────────────────────────────────────────────────────────────────────
# 3. Compile CLI sources (spirv_preprocess.cpp + tint_wrapper.cpp + main.cpp)
# ─────────────────────────────────────────────────────────────────────────────
echo "[3/4] Compiling CLI sources..."

# spirv_preprocess.cpp — compiled with shim include path (before repo root so
# the shim core/templates/ is found instead of Godot's).
compile_one "drivers/webgpu/spirv_preprocess.cpp" \
    "$BUILD_DIR/cli/spirv_preprocess.o" \
    "c++17" \
    -I"$SHIM_DIR" \
    "${SPIRV_TOOLS_INCLUDES[@]}" &

# tint_wrapper.cpp — compiled with Tint C++20 environment.
compile_one "drivers/webgpu/tint_wrapper.cpp" \
    "$BUILD_DIR/cli/tint_wrapper.o" \
    "c++20" \
    "${TINT_INCLUDES[@]}" "${TINT_DEFINES[@]}" \
    -I"drivers/webgpu/" &

# tint_ir_transforms.cpp — Tint-IR-level polyfills used by tint_wrapper.cpp;
# same environment as tint_wrapper.cpp (see tint_ir_transforms.h).
compile_one "drivers/webgpu/tint_ir_transforms.cpp" \
    "$BUILD_DIR/cli/tint_ir_transforms.o" \
    "c++20" \
    "${TINT_INCLUDES[@]}" "${TINT_DEFINES[@]}" \
    -I"drivers/webgpu/" &

# main.cpp — compiled with shim + Tint includes.
compile_one "drivers/webgpu/tint_cli/main.cpp" \
    "$BUILD_DIR/cli/main.o" \
    "c++20" \
    -I"$SHIM_DIR" \
    -I"drivers/webgpu/" \
    "${TINT_INCLUDES[@]}" "${TINT_DEFINES[@]}" &

wait

# ─────────────────────────────────────────────────────────────────────────────
# 4. Link
# ─────────────────────────────────────────────────────────────────────────────
echo "[4/4] Linking tint_convert_cli..."

# Filter to only .o files that were successfully compiled.
LINK_OBJS=("$BUILD_DIR/cli/main.o" "$BUILD_DIR/cli/spirv_preprocess.o" "$BUILD_DIR/cli/tint_wrapper.o" "$BUILD_DIR/cli/tint_ir_transforms.o")
for obj in "${SPIRV_TOOLS_OBJS[@]}" "${TINT_OBJS[@]}"; do
    [[ -f "$obj" ]] && LINK_OBJS+=("$obj")
done

echo "  Linking ${#LINK_OBJS[@]} objects..."

mkdir -p bin
# Pass the object list through a response file: a couple of thousand paths
# overflow Windows' 32K command-line limit (and are harmless elsewhere).
LINK_RSP="$BUILD_DIR/link.rsp"
printf '"%s"
' "${LINK_OBJS[@]}" >"$LINK_RSP"
"$CXX" -o "$OUTPUT" "@$LINK_RSP" -O2 ${CXXFLAGS:-} ${LDFLAGS:-}

echo ""
echo "Built: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
