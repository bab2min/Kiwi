#!/bin/bash

set -e

cd "$(dirname "$0")"

# Generate the package structure
mkdir -p package/src/build
mkdir -p package/dist

# Build the wasm module
mkdir -p build
cd build
emcmake cmake -DCMAKE_BUILD_TYPE=Release -DKIWI_USE_CPUINFO=OFF -DKIWI_BUILD_TEST=OFF -DKIWI_USE_MIMALLOC=OFF -DKIWI_BUILD_CLI=OFF -DKIWI_BUILD_EVALUATOR=OFF -DKIWI_BUILD_MODEL_BUILDER=OFF ../../../
make -j8

# Copy the generated files to the package
cp bindings/wasm/kiwi-wasm.js ../package/src/build/kiwi-wasm.js
cp bindings/wasm/kiwi-wasm.wasm ../package/dist/kiwi-wasm.wasm

# Build typescript wrapper package
cd ../package
npm install
npm run build
