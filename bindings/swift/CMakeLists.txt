# CMakeLists.txt for iOS/macOS Swift bindings
cmake_minimum_required(VERSION 3.19)

# This file is used to build Kiwi as a static library for iOS/macOS
# to be bundled into an XCFramework for Swift Package Manager

project(kiwi_swift LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable options that are not needed for Swift bindings
set(KIWI_BUILD_DYNAMIC OFF CACHE BOOL "Build dynamic library" FORCE)
set(KIWI_BUILD_CLI OFF CACHE BOOL "Build CLI tool" FORCE)
set(KIWI_BUILD_EVALUATOR OFF CACHE BOOL "Build Evaluator" FORCE)
set(KIWI_BUILD_MODEL_BUILDER OFF CACHE BOOL "Build Model Builder" FORCE)
set(KIWI_BUILD_TEST OFF CACHE BOOL "Build Test sets" FORCE)
set(KIWI_JAVA_BINDING OFF CACHE BOOL "Build Java binding" FORCE)

# Include the main Kiwi project
include(../../CMakeLists.txt)

# The main CMakeLists.txt should create the kiwi_static target
# which we'll use for the XCFramework
