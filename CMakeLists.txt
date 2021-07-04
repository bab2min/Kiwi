cmake_minimum_required(VERSION 3.9)

project(kiwi VERSION 0.10.0 DESCRIPTION "Kiwi, Korean Intelligent Word Identifier")

set ( CMAKE_CXX_STANDARD 11 )

set ( CMAKE_BUILD_TYPE Release )
set ( CMAKE_VERBOSE_MAKEFILE true )

set ( CORE_SRCS
  src/core/KFeatureTestor.cpp
  src/core/KForm.cpp
  src/core/Kiwi.cpp
  src/core/KModelMgr.cpp
  src/core/KNLangModel.cpp
  src/core/KTrie.cpp
  src/core/KWordDetector.cpp
  src/core/PatternMatcher.cpp
  src/core/serializer.cpp
  src/core/Utils.cpp
)

link_libraries ( pthread )
set ( CMAKE_EXE_LINKER_FLAGS "-static -Wl,--gc-sections" )

set ( CMAKE_C_FLAGS_DEBUG "-DDEBUG -DC_FLAGS" )
set ( CMAKE_EXE_LINKER_FLAGS_DEBUG "-DDEBUG -DLINKER_FLAGS" )

set ( CMAKE_C_FLAGS_RELEASE "-DRELEASE -DC_FLAGS" )
set ( CMAKE_EXE_LINKER_FLAGS_RELEASE "-DRELEASE -DLINKER_FLAGS" )

#set ( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BUILD_TYPE} )
#set ( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BUILD_TYPE}/lib )
#set ( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BUILD_TYPE}/lib )

add_executable( "${CMAKE_PROJECT_NAME}-cli-${PROJECT_VERSION}"
  ${CORE_SRCS} 
  src/runner.cpp
)

add_library( "${CMAKE_PROJECT_NAME}" SHARED
  ${CORE_SRCS} 
  src/kiwi_c.cpp
)

target_compile_definitions("${CMAKE_PROJECT_NAME}"
  PUBLIC DLL_EXPORT=1
)

set_target_properties("${CMAKE_PROJECT_NAME}" PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER 
    src/kiwi_c.h
)

include(ExternalProject)
ExternalProject_Add(gtest
  URL https://github.com/google/googletest/archive/release-1.8.1.zip
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
  INSTALL_COMMAND ""
)
ExternalProject_Get_Property(gtest source_dir binary_dir)
add_subdirectory(test)