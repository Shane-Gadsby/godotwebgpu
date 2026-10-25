#!/usr/bin/env bash
# Build SPIR-V preprocessing fuzz targets.
#
# Usage:
#   ./drivers/webgpu/wgsl_cli/fuzz/build_fuzzers.sh           # Build all 3 fuzzers
#   ./drivers/webgpu/wgsl_cli/fuzz/build_fuzzers.sh --clean   # Clean + rebuild
#   ./drivers/webgpu/wgsl_cli/fuzz/build_fuzzers.sh --test    # Build + smoke test
#
# Output: bin/fuzz_preprocess_passes, bin/fuzz_split_samplers, bin/fuzz_spirv_to_wgsl
#
# fuzz_spirv_to_wgsl runs the passes and then the SPIRV-Cross WGSL wrapper. All three reuse
# the objects wgsl_cli/build.sh made (it is run first if they are missing).
#
# If Homebrew LLVM is installed (brew install llvm), builds with libFuzzer +
# AddressSanitizer for production fuzzing. Otherwise builds with a standalone
# mutation driver that works with any compiler.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WGSL_CLI_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$WGSL_CLI_DIR/../../../" && pwd)"
BUILD_DIR="$WGSL_CLI_DIR/.build/$(uname -s | cut -d_ -f1 | tr '[:upper:]' '[:lower:]')"
CLI_DIR="$BUILD_DIR/cli"
S2W_OUT="fuzz_spirv_to_wgsl"
SHIM_DIR="$WGSL_CLI_DIR"

# Parse args.
CLEAN=false
SMOKE_TEST=false
for arg in "$@"; do
    [[ "$arg" == "--clean" ]] && CLEAN=true
    [[ "$arg" == "--test" ]] && SMOKE_TEST=true
done

if [[ "$CLEAN" == true ]]; then
    rm -f "$REPO_ROOT/bin/fuzz_preprocess_passes"
    rm -f "$REPO_ROOT/bin/fuzz_split_samplers"
    rm -f "$REPO_ROOT/bin/fuzz_spirv_to_wgsl"
    rm -f "$BUILD_DIR/fuzz/"*.o
fi

# ─────────────────────────────────────────────────────────────────────────────
# 1. Ensure wgsl_cli objects are built (spirv_preprocess.o, spirv_cross_wrapper.o, etc.)
# ─────────────────────────────────────────────────────────────────────────────
if [[ ! -f "$CLI_DIR/spirv_preprocess.o" ]]; then
    echo "[fuzz] Building wgsl_cli dependencies first..."
    OUTPUT="$REPO_ROOT/bin/wgsl_convert_cli.fuzzdeps" bash "$WGSL_CLI_DIR/build.sh"
    rm -f "$REPO_ROOT/bin/wgsl_convert_cli.fuzzdeps"
fi

# ─────────────────────────────────────────────────────────────────────────────
# 2. Detect compiler with libFuzzer support.
#    Priority: CXX env var → Homebrew LLVM (macOS) → system clang++ → fallback.
# ─────────────────────────────────────────────────────────────────────────────
USE_LIBFUZZER=false
FUZZ_TEST_SRC='extern "C" int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long s){return 0;}'

try_libfuzzer() {
    local compiler="$1"
    if echo "$FUZZ_TEST_SRC" | "$compiler" -x c++ -fsanitize=fuzzer,address -o /dev/null - 2>/dev/null; then
        CXX="$compiler"
        USE_LIBFUZZER=true
        return 0
    fi
    return 1
}

# If CXX is explicitly set, try it first.
if [[ -n "${CXX:-}" ]]; then
    try_libfuzzer "$CXX" || true
fi

# Try Homebrew LLVM (macOS).
if [[ "$USE_LIBFUZZER" == false ]]; then
    LLVM_PREFIX="$(brew --prefix llvm 2>/dev/null || echo "")"
    if [[ -n "$LLVM_PREFIX" && -x "$LLVM_PREFIX/bin/clang++" ]]; then
        try_libfuzzer "$LLVM_PREFIX/bin/clang++" || true
    fi
fi

# Try system clang++ (Linux typically ships libFuzzer with clang).
if [[ "$USE_LIBFUZZER" == false ]]; then
    for candidate in clang++ clang++-18 clang++-17 clang++-16 clang++-15; do
        if command -v "$candidate" &>/dev/null; then
            try_libfuzzer "$candidate" && break || true
        fi
    done
fi

if [[ "$USE_LIBFUZZER" == false ]]; then
    CXX="${CXX:-c++}"
    echo "[fuzz] libFuzzer not available — building with standalone mutation driver."
    echo "       macOS: brew install llvm"
    echo "       Linux: apt install clang (or clang-18)"
else
    echo "[fuzz] Using libFuzzer + ASan ($CXX)"
fi

# ─────────────────────────────────────────────────────────────────────────────
# 3. Gather include/object paths.
# ─────────────────────────────────────────────────────────────────────────────
SPIRV_TOOLS_DIR="$REPO_ROOT/thirdparty/spirv-tools"
SPIRV_HEADERS_DIR="$REPO_ROOT/thirdparty/spirv-headers"

PREPROCESS_INCLUDES=(
    -I"$SHIM_DIR"
    -I"$SPIRV_TOOLS_DIR"
    -I"$SPIRV_TOOLS_DIR/source/"
    -I"$SPIRV_TOOLS_DIR/include/"
    -I"$SPIRV_TOOLS_DIR/generated/"
    -I"$SPIRV_HEADERS_DIR/include/"
    -I"$SPIRV_HEADERS_DIR/include/spirv/unified1/"
)

# Collect library objects.
SPIRV_TOOLS_OBJS=()
while IFS= read -r obj; do
    SPIRV_TOOLS_OBJS+=("$obj")
done < <(find "$BUILD_DIR/spirv_tools" -name '*.o' 2>/dev/null)

SPIRV_CROSS_OBJS=()
while IFS= read -r obj; do
    SPIRV_CROSS_OBJS+=("$obj")
done < <(find "$BUILD_DIR/spirv_cross" -name '*.o' 2>/dev/null)

# spirv_preprocess.o calls into spirv_lite_reflect.o and spirv_opt_passes.o,
# and all three into SPIRV-Tools.
PREPROCESS_OBJS=("$CLI_DIR/spirv_preprocess.o" "$CLI_DIR/spirv_lite_reflect.o" "$CLI_DIR/spirv_opt_passes.o" "${SPIRV_TOOLS_OBJS[@]}")
WRAPPER_OBJS=("$CLI_DIR/spirv_cross_wrapper.o" "${SPIRV_CROSS_OBJS[@]}")
WRAPPER_FLAGS=(-std=c++17)

# ─────────────────────────────────────────────────────────────────────────────
# 4. Compile and link fuzz targets.
# ─────────────────────────────────────────────────────────────────────────────
mkdir -p "$BUILD_DIR/fuzz" "$REPO_ROOT/bin"

COMMON_FLAGS="-O1 -g -std=c++17 -w"

if [[ "$USE_LIBFUZZER" == true ]]; then
    FUZZ_FLAGS="-fsanitize=fuzzer,address"
    DRIVER_OBJ=""
else
    FUZZ_FLAGS=""
    # Compile standalone driver.
    echo "[fuzz] Compiling standalone driver..."
    $CXX -c "$SCRIPT_DIR/standalone_driver.cpp" \
        -o "$BUILD_DIR/fuzz/standalone_driver.o" \
        $COMMON_FLAGS
    DRIVER_OBJ="$BUILD_DIR/fuzz/standalone_driver.o"
fi

# Our own pass sources are compiled again here, instrumented. The objects build.sh made are plain -O2: linked
# into a fuzzer, libFuzzer saw coverage from the harness file alone (~200 edges), mutated blind, and ASan
# could not see an out-of-bounds read in a raw word pass. SPIRV-Tools and the backend stay uninstrumented
# (a crash there still aborts, so it is still found; it just does not steer the mutations).
if [[ "$USE_LIBFUZZER" == true ]]; then
    echo "[fuzz] Compiling the passes with coverage + ASan..."
    INSTR_FLAGS=(-O1 -g -std=c++17 -w -fsanitize=fuzzer-no-link,address)
    $CXX -c "$REPO_ROOT/drivers/webgpu/spirv_preprocess.cpp" -o "$BUILD_DIR/fuzz/spirv_preprocess.o" \
        "${INSTR_FLAGS[@]}" "${PREPROCESS_INCLUDES[@]}" &
    INSTR_PIDS=($!)
    $CXX -c "$REPO_ROOT/drivers/webgpu/spirv_opt_passes.cpp" -o "$BUILD_DIR/fuzz/spirv_opt_passes.o" \
        "${INSTR_FLAGS[@]}" "${PREPROCESS_INCLUDES[@]}" &
    INSTR_PIDS+=($!)
    $CXX -c "$REPO_ROOT/drivers/webgpu/spirv_lite_reflect.cpp" -o "$BUILD_DIR/fuzz/spirv_lite_reflect.o" \
        "${INSTR_FLAGS[@]}" -I"$SHIM_DIR" &
    INSTR_PIDS+=($!)
    # wait with no pid always succeeds; waiting on each one makes a compile error stop the script.
    for pid in "${INSTR_PIDS[@]}"; do wait "$pid"; done
    PREPROCESS_OBJS=("$BUILD_DIR/fuzz/spirv_preprocess.o" "$BUILD_DIR/fuzz/spirv_lite_reflect.o" "$BUILD_DIR/fuzz/spirv_opt_passes.o" "${SPIRV_TOOLS_OBJS[@]}")
fi

# --- fuzz_preprocess_passes (preprocessing only, no converter) ---
echo "[fuzz] Building fuzz_preprocess_passes..."
$CXX -c "$SCRIPT_DIR/fuzz_preprocess_passes.cpp" \
    -o "$BUILD_DIR/fuzz/fuzz_preprocess_passes.o" \
    $COMMON_FLAGS "${PREPROCESS_INCLUDES[@]}" $FUZZ_FLAGS

$CXX -o "$REPO_ROOT/bin/fuzz_preprocess_passes" \
    "$BUILD_DIR/fuzz/fuzz_preprocess_passes.o" \
    "${PREPROCESS_OBJS[@]}" \
    $DRIVER_OBJ \
    $FUZZ_FLAGS -O1

# --- fuzz_split_samplers (single pass, no converter) ---
echo "[fuzz] Building fuzz_split_samplers..."
$CXX -c "$SCRIPT_DIR/fuzz_split_samplers.cpp" \
    -o "$BUILD_DIR/fuzz/fuzz_split_samplers.o" \
    $COMMON_FLAGS "${PREPROCESS_INCLUDES[@]}" $FUZZ_FLAGS

$CXX -o "$REPO_ROOT/bin/fuzz_split_samplers" \
    "$BUILD_DIR/fuzz/fuzz_split_samplers.o" \
    "${PREPROCESS_OBJS[@]}" \
    $DRIVER_OBJ \
    $FUZZ_FLAGS -O1

# --- fuzz_spirv_to_wgsl (full pipeline: preprocessing + SPIRV-Cross) ---
echo "[fuzz] Building $S2W_OUT..."
$CXX -c "$SCRIPT_DIR/fuzz_spirv_to_wgsl.cpp" \
    -o "$BUILD_DIR/fuzz/fuzz_spirv_to_wgsl.o" \
    -O1 -g -w -fexceptions \
    "${PREPROCESS_INCLUDES[@]}" "${WRAPPER_FLAGS[@]}" \
    -I"$REPO_ROOT/drivers/webgpu/" \
    $FUZZ_FLAGS

$CXX -o "$REPO_ROOT/bin/$S2W_OUT" \
    "$BUILD_DIR/fuzz/fuzz_spirv_to_wgsl.o" \
    "${PREPROCESS_OBJS[@]}" \
    "${WRAPPER_OBJS[@]}" \
    $DRIVER_OBJ \
    $FUZZ_FLAGS -O1

echo ""
echo "[fuzz] Built:"
for f in fuzz_preprocess_passes fuzz_split_samplers "$S2W_OUT"; do
    printf "  bin/%-30s %s\n" "$f" "$(du -h "$REPO_ROOT/bin/$f" | cut -f1)"
done

# ─────────────────────────────────────────────────────────────────────────────
# 5. Set up seed corpus from fixtures.
# ─────────────────────────────────────────────────────────────────────────────
CORPUS_DIR="$SCRIPT_DIR/corpus"
FIXTURES_DIR="$REPO_ROOT/webgpu_tests/shader_corpus/fixtures"

if [[ ! -d "$CORPUS_DIR" ]]; then
    echo ""
    echo "[fuzz] Setting up seed corpus from fixtures..."
    mkdir -p "$CORPUS_DIR"
    for spv in "$FIXTURES_DIR"/*.spv; do
        [[ -f "$spv" ]] && cp "$spv" "$CORPUS_DIR/"
    done
    echo "  Copied $(ls "$CORPUS_DIR"/*.spv 2>/dev/null | wc -l | tr -d ' ') files"
fi

# ─────────────────────────────────────────────────────────────────────────────
# 6. Smoke test (if --test).
# ─────────────────────────────────────────────────────────────────────────────
if [[ "$SMOKE_TEST" == true ]]; then
    echo ""
    echo "[fuzz] Running smoke test (feed each fixture to each fuzzer)..."
    CORPUS_FILES=("$CORPUS_DIR"/*.spv)

    # libFuzzer ignores --flags and reads a bare "0" as a corpus directory; it takes -runs=0 and the directory.
    if [[ "$USE_LIBFUZZER" == true ]]; then
        SMOKE_ARGS=(-runs=0 "$CORPUS_DIR")
    else
        SMOKE_ARGS=(--iterations 0 "${CORPUS_FILES[@]}")
    fi
    for fuzzer in fuzz_preprocess_passes fuzz_split_samplers "$S2W_OUT"; do
        printf "  %-30s" "$fuzzer"
        if "$REPO_ROOT/bin/$fuzzer" "${SMOKE_ARGS[@]}" 2>/dev/null; then
            echo "PASS"
        else
            echo "FAIL (exit $?)"
        fi
    done
fi

echo ""
if [[ "$USE_LIBFUZZER" == true ]]; then
    echo "Run a fuzzer:  bin/fuzz_preprocess_passes drivers/webgpu/wgsl_cli/fuzz/corpus/"
else
    echo "Run a fuzzer:  bin/fuzz_preprocess_passes --iterations 100000 drivers/webgpu/wgsl_cli/fuzz/corpus/*.spv"
fi
