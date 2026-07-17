#!/usr/bin/env bash
set -euo pipefail

print_usage() {
    cat <<'EOF'
Usage:
  ./build_macos.sh [debug|release] [--test] [--rhi] [--clean] [-j N]

Examples:
  ./build_macos.sh
  ./build_macos.sh debug --test
  ./build_macos.sh release -j 8

Options:
  debug      Build the macos-debug CMake preset. This is the default.
  release    Build the macos-release CMake preset.
  --test     Run the matching CTest preset when available.
  --rhi      Run the native Metal RHI acceptance test. Intended for Debug builds.
  --clean    Remove the selected preset build directory before configuring.
  -j N       Parallel build jobs. Defaults to the number of local CPU cores.
  -h,--help  Show this help.
EOF
}

mode="debug"
run_tests=0
run_rhi=0
clean=0
jobs="$(sysctl -n hw.ncpu 2>/dev/null || echo 6)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        debug|Debug|DEBUG)
            mode="debug"
            shift
            ;;
        release|Release|RELEASE)
            mode="release"
            shift
            ;;
        --test)
            run_tests=1
            shift
            ;;
        --rhi)
            run_rhi=1
            shift
            ;;
        --clean)
            clean=1
            shift
            ;;
        -j)
            if [[ $# -lt 2 ]]; then
                echo "Missing value after -j" >&2
                exit 2
            fi
            jobs="$2"
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            print_usage >&2
            exit 2
            ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build_macos.sh must be run on macOS." >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

preset="macos-${mode}"
build_dir="build/macos-preset-${mode}"

if [[ "$clean" -eq 1 ]]; then
    echo "Removing $build_dir"
    rm -rf "$build_dir"
fi

echo "Configuring preset: $preset"
cmake --preset "$preset"

echo "Building preset: $preset (-j $jobs)"
cmake --build --preset "$preset" -j "$jobs"

if [[ "$run_tests" -eq 1 ]]; then
    if [[ "$mode" == "debug" ]]; then
        echo "Running tests: macos-debug"
        ctest --preset macos-debug --output-on-failure
    else
        echo "Release preset does not define a CTest preset; skipping --test for release."
    fi
fi

if [[ "$run_rhi" -eq 1 ]]; then
    echo "Running native Metal RHI acceptance tests"
    ctest --preset macos-rhi-acceptance --output-on-failure
fi

if [[ "$mode" == "debug" ]]; then
    echo "App bundle: $script_dir/build/macos-preset-debug/src/ISPImageViewerDebug.app"
else
    echo "App bundle: $script_dir/build/macos-preset-release/src/ISPImageViewer.app"
fi
