# iOS binding CMakeLists.txt for Kiwi
# This is a work-in-progress implementation following the iOS roadmap

# iOS binding requires Xcode and iOS SDK
if(NOT IOS)
    message(STATUS "iOS binding requires iOS SDK. Use -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake")
    return()
endif()

# Check for required iOS development tools
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR NOT CMAKE_TOOLCHAIN_FILE MATCHES "ios")
    message(WARNING "iOS binding requires iOS toolchain. Consider using ios-cmake toolchain.")
endif()

set(pkg_name "KiwiSwift")

# Collect required object files
set(OBJECTS $<TARGET_OBJECTS:${PROJECT_NAME}_static> $<TARGET_OBJECTS:streamvbyte>)

if(KIWI_USE_CPUINFO)
    list(APPEND OBJECTS $<TARGET_OBJECTS:cpuinfo>)
endif()

# Create static library for iOS (required for App Store distribution)
add_library(${pkg_name} STATIC 
    csrc/kiwi_swift.cpp
    ${OBJECTS}
)

# Set iOS-specific compile features and options
target_compile_features(${pkg_name} PUBLIC cxx_std_17)

# iOS-specific compile definitions
target_compile_definitions(${pkg_name} PRIVATE 
    IOS=1
    KIWI_IOS_BINDING=1
)

# Optimize for mobile performance
target_compile_options(${pkg_name} PRIVATE 
    -O3
    -fvisibility=hidden
    -fvisibility-inlines-hidden
)

# Set up framework structure for iOS
set_target_properties(${pkg_name} PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER com.bab2min.kiwi
    PUBLIC_HEADER ""
    OUTPUT_NAME "Kiwi"
)

# Installation rules for iOS framework
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

install(TARGETS ${pkg_name}
    FRAMEWORK DESTINATION .
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Copy Swift interface and headers
install(FILES 
    "include/Kiwi.h"
    DESTINATION include
)

message(STATUS "iOS binding configured for ${CMAKE_OSX_ARCHITECTURES}")
message(STATUS "iOS Deployment Target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")