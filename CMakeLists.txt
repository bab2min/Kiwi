cmake_minimum_required(VERSION 3.9)

project(kiwi VERSION 0.10.0 DESCRIPTION "Kiwi, Korean Intelligent Word Identifier")

set ( CMAKE_CXX_STANDARD 11 )
set ( CMAKE_VERBOSE_MAKEFILE true )

option(KIWI_USE_MIMALLOC  "Use mimalloc for faster memory allocation" ON)

if (NOT CMAKE_BUILD_TYPE)
  if ("${CMAKE_BINARY_DIR}" MATCHES ".*(D|d)ebug$")
    message(STATUS "No build type selected, default to: Debug")
    set(CMAKE_BUILD_TYPE "Debug")
  else()
    message(STATUS "No build type selected, default to: Release")
    set(CMAKE_BUILD_TYPE "Release")
  endif()
endif()

set ( CORE_SRCS
  src/Form.cpp
  src/FeatureTestor.cpp
  src/Kiwi.cpp
  src/KiwiBuilder.cpp
  src/KTrie.cpp
  src/PatternMatcher.cpp
  src/Utils.cpp
  src/WordDetector.cpp
)


if(KIWI_USE_MIMALLOC)
  message(STATUS "Use mimalloc allocators")
  set ( ADDITIONAL_FLAGS "-DKIWI_USE_MIMALLOC" )
  include_directories( third_party/mimalloc/include )
  set ( CORE_SRCS "${CORE_SRCS}"
    third_party/mimalloc/src/static.c
  )
endif()


include_directories( include/ )
include_directories( third_party/tclap/include )
include_directories( third_party/cpp-btree )

if(MSVC)
  set ( CMAKE_C_FLAGS_DEBUG "-DDEBUG -DC_FLAGS -Zi -Od ${ADDITIONAL_FLAGS}" )
  set ( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" )

  set ( CMAKE_C_FLAGS_RELEASE "-DNDEBUG -DRELEASE -DC_FLAGS -O2 -Oi -Gy -DKIWI_USE_BTREE ${ADDITIONAL_FLAGS}" )
  set ( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}" )

  set ( CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -Zi")
  set ( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
  set ( CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" )
else()
  link_libraries ( pthread )
  
  set ( CMAKE_C_FLAGS_DEBUG "-DDEBUG -DC_FLAGS -g3 -O0 ${ADDITIONAL_FLAGS}" )
  set ( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" )
  set ( CMAKE_EXE_LINKER_FLAGS_DEBUG "-DDEBUG -DLINKER_FLAGS" )

  set ( CMAKE_C_FLAGS_RELEASE "-DNDEBUG -DRELEASE -DC_FLAGS -O3 -DKIWI_USE_BTREE ${ADDITIONAL_FLAGS}" )
  set ( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}" )
  set ( CMAKE_EXE_LINKER_FLAGS_RELEASE "-DRELEASE -DLINKER_FLAGS" )

  set ( CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -g3")
  set ( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
  set ( CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" )
endif()

add_library( "${CMAKE_PROJECT_NAME}_static" STATIC
  ${CORE_SRCS} 
  src/capi/kiwi_c.cpp
)

add_library( "${CMAKE_PROJECT_NAME}" SHARED
  ${CORE_SRCS} 
  src/capi/kiwi_c.cpp
)

add_executable( "${CMAKE_PROJECT_NAME}-cli-${PROJECT_VERSION}"
  tools/runner.cpp
)

target_link_libraries( "${CMAKE_PROJECT_NAME}-cli-${PROJECT_VERSION}"
  "${CMAKE_PROJECT_NAME}_static"
)

add_executable( "${CMAKE_PROJECT_NAME}-evaluator"
  tools/Evaluator.cpp
  tools/evaluator_main.cpp
)

target_link_libraries( "${CMAKE_PROJECT_NAME}-evaluator"
  "${CMAKE_PROJECT_NAME}_static"
)

if(MSVC)
  target_compile_options("${CMAKE_PROJECT_NAME}_static" PUBLIC
    "/MT"
  )

  add_library( "${CMAKE_PROJECT_NAME}_rtd" SHARED
    ${CORE_SRCS} 
    src/capi/kiwi_c.cpp
  )

  target_compile_options("${CMAKE_PROJECT_NAME}_rtd" PUBLIC
    "/MD"
  )
endif()

target_compile_definitions("${CMAKE_PROJECT_NAME}"
  PUBLIC DLL_EXPORT=1
)

set_target_properties("${CMAKE_PROJECT_NAME}" PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER 
    include/kiwi/capi.h
)

add_subdirectory( third_party/googletest )
add_subdirectory( test )
