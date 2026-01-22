#!/bin/bash
set -e

# Build script for creating XCFramework for iOS and macOS
# This script builds the Kiwi library for multiple platforms and architectures
# and combines them into a single XCFramework

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SWIFT_DIR="$PROJECT_ROOT/bindings/swift"
BUILD_DIR="$SWIFT_DIR/build"
XCFRAMEWORK_DIR="$SWIFT_DIR/xcframework"

# Clean previous builds
echo "Cleaning previous builds..."
rm -rf "$BUILD_DIR"
rm -rf "$XCFRAMEWORK_DIR"

mkdir -p "$BUILD_DIR"
mkdir -p "$XCFRAMEWORK_DIR"

# Function to build for a specific platform
build_platform() {
    local PLATFORM=$1
    local SDK=$2
    local ARCHS=$3
    local DEPLOYMENT_TARGET=$4
    local BUILD_SUBDIR=$5
    
    echo "Building for $PLATFORM ($ARCHS)..."
    
    local PLATFORM_BUILD_DIR="$BUILD_DIR/$BUILD_SUBDIR"
    mkdir -p "$PLATFORM_BUILD_DIR"
    
    cd "$PLATFORM_BUILD_DIR"
    
    cmake "$PROJECT_ROOT" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=$PLATFORM \
        -DCMAKE_OSX_SYSROOT=$SDK \
        -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET \
        -DKIWI_BUILD_DYNAMIC=OFF \
        -DKIWI_BUILD_CLI=OFF \
        -DKIWI_BUILD_EVALUATOR=OFF \
        -DKIWI_BUILD_MODEL_BUILDER=OFF \
        -DKIWI_BUILD_TEST=OFF \
        -DKIWI_JAVA_BINDING=OFF \
        -DKIWI_USE_MIMALLOC=ON \
        -DKIWI_USE_CPUINFO=OFF \
        -GXcode
    
    cmake --build . --config Release
    
    # Create framework structure
    local FRAMEWORK_DIR="$PLATFORM_BUILD_DIR/Kiwi.framework"
    mkdir -p "$FRAMEWORK_DIR/Headers"
    
    # Copy library
    cp Release/libkiwi_static.a "$FRAMEWORK_DIR/Kiwi"
    
    # Copy headers
    cp "$PROJECT_ROOT/include/kiwi/capi.h" "$FRAMEWORK_DIR/Headers/"
    cp "$PROJECT_ROOT/include/kiwi/Macro.h" "$FRAMEWORK_DIR/Headers/"
    
    # Create module map
    cat > "$FRAMEWORK_DIR/Modules/module.modulemap" << EOF
framework module Kiwi {
    umbrella header "capi.h"
    export *
    module * { export * }
}
EOF
    
    echo "✓ Built $PLATFORM"
}

# Build for iOS Device (arm64)
build_platform "iOS" "iphoneos" "arm64" "12.0" "ios-arm64"

# Build for iOS Simulator (arm64 + x86_64)
build_platform "iOS" "iphonesimulator" "arm64;x86_64" "12.0" "ios-simulator"

# Build for macOS (arm64 + x86_64 universal)
build_platform "Darwin" "macosx" "arm64;x86_64" "10.14" "macos"

# Create XCFramework
echo "Creating XCFramework..."
xcodebuild -create-xcframework \
    -framework "$BUILD_DIR/ios-arm64/Kiwi.framework" \
    -framework "$BUILD_DIR/ios-simulator/Kiwi.framework" \
    -framework "$BUILD_DIR/macos/Kiwi.framework" \
    -output "$XCFRAMEWORK_DIR/Kiwi.xcframework"

echo "✓ XCFramework created at $XCFRAMEWORK_DIR/Kiwi.xcframework"

# Calculate checksum for Swift Package Manager
cd "$XCFRAMEWORK_DIR"
CHECKSUM=$(swift package compute-checksum Kiwi.xcframework.zip 2>/dev/null || echo "N/A")
echo "Checksum: $CHECKSUM"

echo ""
echo "Build complete!"
echo "XCFramework location: $XCFRAMEWORK_DIR/Kiwi.xcframework"
