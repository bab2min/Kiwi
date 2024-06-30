#!/usr/bin/env bash

set -e

# Change to the script directory
cd "$(dirname "$0")"

# Get root directory
REPO_ROOT_DIR=$(git rev-parse --show-toplevel)

# Check if emscripten is installed
if ! command -v emcmake &> /dev/null; then
    echo "Emscripten is not installed. Please install it and make sure it is in your PATH."
    exit 1
fi

# Generate the package structure
mkdir -p package/src/build
mkdir -p package/dist

# Find core count for make. Prefer nproc, then sysctl, then default to 1
if command -v nproc &> /dev/null; then
    CORE_COUNT=$(nproc)
elif command -v sysctl &> /dev/null; then
    CORE_COUNT=$(sysctl -n hw.logicalcpu)
else
    CORE_COUNT=1
fi

# Build the wasm module and read the project version
mkdir -p build
cd build
emcmake cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DKIWI_USE_CPUINFO=OFF \
    -DKIWI_USE_MIMALLOC=OFF \
    -DKIWI_BUILD_TEST=OFF \
    -DKIWI_BUILD_CLI=OFF \
    -DKIWI_BUILD_EVALUATOR=OFF \
    -DKIWI_BUILD_MODEL_BUILDER=OFF \
    $REPO_ROOT_DIR
make -j $CORE_COUNT
PROJECT_VERSION=$(grep -m 1 CMAKE_PROJECT_VERSION:STATIC CMakeCache.txt | cut -d'=' -f2)
if [ -z "$PROJECT_VERSION" ]; then
    echo "Failed to read project version from CMakeCache.txt"
    exit 1
fi
cd ..

# Copy the generated files to the package
cp build/bindings/wasm/kiwi-wasm.js package/src/build/kiwi-wasm.js
cp build/bindings/wasm/kiwi-wasm.wasm package/dist/kiwi-wasm.wasm

# Build typescript wrapper package and update the version
cd package
npm install
npm run build
npm version --no-git-tag-version --allow-same-version $PROJECT_VERSION
cd ..

# Build the demo package if --demo or --demo-dev is passed
# --demo with create a static build
# --demo-dev will start a development server
if [ "$1" == "--demo" ] || [ "$1" == "--demo-dev" ]; then
    cd package-demo
    npm install
    if [ "$1" == "--demo-dev" ]; then
        npm run dev
    else
        npm run build
    fi
    cd ..
fi
