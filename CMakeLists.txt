cmake_minimum_required(VERSION 3.9)

project(kiwi VERSION 0.10.0 DESCRIPTION "Kiwi, Korean Intelligent Word Identifier")

set ( CMAKE_CXX_STANDARD 11 )
set ( CMAKE_VERBOSE_MAKEFILE true )

option(KIWI_USE_MIMALLOC  "Use mimalloc for faster memory allocation" ON)
option(KIWI_STATIC_WITHOUT_MT  "Use /MT Option in building kiwi_static" OFF)
option(KIWI_BUILD_TEST  "Build Test sets" ON)

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
  set ( CMAKE_C_FLAGS_DEBUG "-DDEBUG -DC_FLAGS -Zi -Od /utf-8" )
  set ( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" )

  set ( CMAKE_C_FLAGS_RELEASE "-DNDEBUG -DRELEASE -DC_FLAGS -O2 -Oi -Gy /utf-8 -DKIWI_USE_BTREE" )
  set ( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}" )

  set ( CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -Zi")
  set ( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
  set ( CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" )
else()
  link_libraries ( pthread )

  set ( CMAKE_C_FLAGS_DEBUG "-DDEBUG -DC_FLAGS -g3 -O0" )
  set ( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" )
  set ( CMAKE_EXE_LINKER_FLAGS_DEBUG "-DDEBUG -DLINKER_FLAGS" )

  set ( CMAKE_C_FLAGS_RELEASE "-DNDEBUG -DRELEASE -DC_FLAGS -O3 -DKIWI_USE_BTREE" )
  set ( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}" )
  set ( CMAKE_EXE_LINKER_FLAGS_RELEASE "-DRELEASE -DLINKER_FLAGS" )

  set ( CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -g3")
  set ( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
  set ( CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" )
endif()

add_library( "${PROJECT_NAME}_static" STATIC
  ${CORE_SRCS}
  src/capi/kiwi_c.cpp
)

add_library( "${PROJECT_NAME}" SHARED
  ${CORE_SRCS}
  src/capi/kiwi_c.cpp
)

# Install the kiwi library as well as header files to (`include/kiwi` directory)
# so that a user can use it in their own projects that are not cmake projects.
file(GLOB KIWI_INCLUDE_FILES include/kiwi/*.h)
set_target_properties(${PROJECT_NAME}
    PROPERTIES PUBLIC_HEADER "${KIWI_INCLUDE_FILES}"
)
install(TARGETS ${PROJECT_NAME} PUBLIC_HEADER DESTINATION include/kiwi)

target_compile_options("${PROJECT_NAME}_static" PRIVATE "${ADDITIONAL_FLAGS}")
target_compile_options("${PROJECT_NAME}" PRIVATE "${ADDITIONAL_FLAGS}")

add_executable( "${PROJECT_NAME}-cli-${PROJECT_VERSION}"
  tools/runner.cpp
)

target_link_libraries( "${PROJECT_NAME}-cli-${PROJECT_VERSION}"
  "${PROJECT_NAME}_static"
)

add_executable( "${PROJECT_NAME}-evaluator"
  tools/Evaluator.cpp
  tools/evaluator_main.cpp
)

target_link_libraries( "${PROJECT_NAME}-evaluator"
  "${PROJECT_NAME}_static"
)

if(MSVC)
  if(KIWI_STATIC_WITHOUT_MT)
    message(STATUS "Use /MD at kiwi_static")
    add_library( "${PROJECT_NAME}_mt_static" STATIC
      ${CORE_SRCS}
      src/capi/kiwi_c.cpp
    )

    target_compile_options("${PROJECT_NAME}_mt_static" PUBLIC
      -MT
    )
  else()
    message(STATUS "Use /MT at kiwi_static")
    target_compile_options("${PROJECT_NAME}_static" PUBLIC
      -MT
    )
  endif()

  target_compile_options("${PROJECT_NAME}" PUBLIC
    -MT
  )
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries( "${PROJECT_NAME}-cli-${PROJECT_VERSION}"
    rt
  )

  target_link_libraries( "${PROJECT_NAME}-evaluator"
    rt
  )
endif()

target_compile_definitions("${PROJECT_NAME}"
  PUBLIC DLL_EXPORT=1
)

set_target_properties("${PROJECT_NAME}" PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER
    include/kiwi/capi.h
)

if(KIWI_BUILD_TEST)
  add_subdirectory( third_party/googletest )
  add_subdirectory( test )
endif()
