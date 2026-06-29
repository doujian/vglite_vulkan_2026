#!/bin/bash
# build.sh - One-click build for vglite_by_vulkan
#
# Linux usage (volk + vendored headers = self-contained build):
#   ./build.sh              # build
#   ./build.sh clean        # clean rebuild
#   ./build.sh test         # build + run ALL tests with Vulkan validation
#   ./build.sh run          # build + run ALL tests (no validation layer)
#
# Runtime: libvulkan.so.1 is loaded via dlopen (volk), so:
#   export LD_LIBRARY_PATH=/path/to/vulkan/lib:$LD_LIBRARY_PATH
#
# Usage:
#   (default)  - Build the project
#   clean      - Clean and rebuild from scratch
#   test       - Build and run all tests with Vulkan validation
#   run        - Build and run all tests (no validation layer)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

# Preserve LD_LIBRARY_PATH for runtime (volk dlopens libvulkan.so.1)
echo "=== vglite_vulkan build ==="
echo "  LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-(not set)}"
if [ -n "$LD_LIBRARY_PATH" ]; then
    echo "  Vulkan loader will search: $LD_LIBRARY_PATH"
fi

case "${1:-build}" in
    clean)
        echo "=== Cleaning build directory ==="
        rm -rf "$BUILD_DIR"
        echo "=== Running CMake ($BUILD_TYPE) ==="
        cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        echo "=== Building ==="
        cmake --build "$BUILD_DIR" -j$(nproc)
        echo "=== Build complete ==="
        ;;
    test)
        echo "=== Building ==="
        if [ ! -d "$BUILD_DIR" ]; then
            cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        fi
        cmake --build "$BUILD_DIR" -j$(nproc)
        echo ""
        echo "=== Running ALL tests with Vulkan validation ==="
        export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
        ALL_TESTS="test_clear test_clear_unit test_clear_dl test_align16 \
                   test_draw_image test_recolor test_tiled \
                   test_gfx1 test_gfx2 test_gfx3 test_gfx21 \
                   test_blend_premultiply test_patternFill test_imgIndex \
                   test_sft_clear test_tiger test_linearGrad test_gradient \
                   test_scissor test_radialGrad \
                   test_imgA8 test_rotate test_scale test_sft_blit"
        PASS=0
        FAIL=0
        SKIP=0
        for t in $ALL_TESTS; do
            exe="$BUILD_DIR/tests/$t"
            if [ ! -x "$exe" ]; then
                echo "--- SKIP $t (not built) ---"
                SKIP=$((SKIP+1))
                continue
            fi
            echo "--- $t ---"
            if (cd "$BUILD_DIR/tests" && ./$t) 2>&1; then
                PASS=$((PASS+1))
                echo "PASSED: $t"
            else
                FAIL=$((FAIL+1))
                echo "FAILED: $t"
            fi
            echo ""
        done
        echo "=== Results: $PASS passed, $FAIL failed, $SKIP skipped ==="
        ;;
    run)
        echo "=== Building ==="
        if [ ! -d "$BUILD_DIR" ]; then
            cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        fi
        cmake --build "$BUILD_DIR" -j$(nproc)
        echo ""
        echo "=== Running ALL tests (no validation layer) ==="
        ALL_TESTS="test_clear test_clear_unit test_clear_dl test_align16 \
                   test_draw_image test_recolor test_tiled \
                   test_gfx1 test_gfx2 test_gfx3 test_gfx21 \
                   test_blend_premultiply test_patternFill test_imgIndex \
                   test_sft_clear test_tiger test_linearGrad test_gradient \
                   test_scissor test_radialGrad \
                   test_imgA8 test_rotate test_scale test_sft_blit"
        for t in $ALL_TESTS; do
            exe="$BUILD_DIR/tests/$t"
            if [ ! -x "$exe" ]; then continue; fi
            echo "--- $t ---"
            (cd "$BUILD_DIR/tests" && ./$t)
            echo ""
        done
        ;;
    build|*)
        echo "=== Building ($BUILD_TYPE) ==="
        if [ ! -d "$BUILD_DIR" ]; then
            cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
        fi
        cmake --build "$BUILD_DIR" -j$(nproc)
        echo "=== Build complete ==="
        ;;
esac
