#!/bin/bash
# build.sh - One-click build for vglite_by_vulkan
# Usage: ./build.sh [clean|test|run]
#   (default)  - Build the project
#   clean      - Clean and rebuild from scratch
#   test       - Build and run all tests with Vulkan validation
#   run        - Build and run all tests (no validation layer)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

case "${1:-build}" in
    clean)
        echo "=== Cleaning build directory ==="
        rm -rf "$BUILD_DIR"
        echo "=== Running CMake ==="
        cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
        echo "=== Building ==="
        cmake --build "$BUILD_DIR" -j$(nproc)
        echo "=== Build complete ==="
        ;;
    test)
        echo "=== Building ==="
        if [ ! -d "$BUILD_DIR" ]; then
            cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
        fi
        cmake --build "$BUILD_DIR" -j$(nproc)
        echo ""
        echo "=== Running tests with Vulkan validation ==="
        export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
        PASS=0
        FAIL=0
        for t in test_clear test_clear_unit test_rotate test_gfx21 test_blend_premultiply test_scale test_align16; do
            echo "--- $t ---"
            if (cd "$BUILD_DIR/tests" && ./$t) 2>&1; then
                PASS=$((PASS+1))
                echo "✅ $t PASSED"
            else
                FAIL=$((FAIL+1))
                echo "❌ $t FAILED"
            fi
            echo ""
        done
        echo "=== Results: $PASS passed, $FAIL failed ==="
        [ $FAIL -eq 0 ] || exit 1
        ;;
    run)
        echo "=== Building ==="
        if [ ! -d "$BUILD_DIR" ]; then
            cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
        fi
        cmake --build "$BUILD_DIR" -j$(nproc)
        echo ""
        echo "=== Running tests ==="
        for t in test_clear test_clear_unit test_rotate test_gfx21 test_blend_premultiply test_scale test_align16; do
            echo "--- $t ---"
            (cd "$BUILD_DIR/tests" && ./$t)
            echo ""
        done
        ;;
    build|*)
        echo "=== Building ==="
        if [ ! -d "$BUILD_DIR" ]; then
            cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
        fi
        cmake --build "$BUILD_DIR" -j$(nproc)
        echo "=== Build complete ==="
        ;;
esac
