cmake_minimum_required(VERSION 3.22.1)

project("kiwi-android")

set(CMAKE_CXX_STANDARD 17)

# Set Android-specific configurations
set(ANDROID 1)
set(KIWI_CPU_ARCH arm64)
set(KIWI_USE_MIMALLOC 0)
set(KIWI_USE_CPUINFO 0)
set(KIWI_BUILD_CLI 0)
set(KIWI_BUILD_EVALUATOR 0)
set(KIWI_BUILD_MODEL_BUILDER 0)
set(KIWI_BUILD_TEST 0)
set(KIWI_JAVA_BINDING 1)

# Paths
set(KIWI_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../)

# Include directories
include_directories(${KIWI_ROOT_DIR}/include)
include_directories(${KIWI_ROOT_DIR}/bindings/java)
include_directories(${KIWI_ROOT_DIR}/third_party/tclap/include)
include_directories(${KIWI_ROOT_DIR}/third_party/cpp-btree)
include_directories(${KIWI_ROOT_DIR}/third_party/eigen)
include_directories(${KIWI_ROOT_DIR}/third_party/json/include)
include_directories(${KIWI_ROOT_DIR}/third_party/streamvbyte/include)

# Core sources
set(CORE_SRCS
  ${KIWI_ROOT_DIR}/src/ArchUtils.cpp
  ${KIWI_ROOT_DIR}/src/Combiner.cpp
  ${KIWI_ROOT_DIR}/src/CoNgramModel.cpp
  ${KIWI_ROOT_DIR}/src/Dataset.cpp
  ${KIWI_ROOT_DIR}/src/Form.cpp
  ${KIWI_ROOT_DIR}/src/FeatureTestor.cpp
  ${KIWI_ROOT_DIR}/src/FileUtils.cpp
  ${KIWI_ROOT_DIR}/src/Joiner.cpp
  ${KIWI_ROOT_DIR}/src/Kiwi.cpp
  ${KIWI_ROOT_DIR}/src/KiwiBuilder.cpp
  ${KIWI_ROOT_DIR}/src/Knlm.cpp
  ${KIWI_ROOT_DIR}/src/KTrie.cpp
  ${KIWI_ROOT_DIR}/src/PatternMatcher.cpp
  ${KIWI_ROOT_DIR}/src/search.cpp
  ${KIWI_ROOT_DIR}/src/ScriptType.cpp
  ${KIWI_ROOT_DIR}/src/SkipBigramModel.cpp
  ${KIWI_ROOT_DIR}/src/SubstringExtractor.cpp
  ${KIWI_ROOT_DIR}/src/SwTokenizer.cpp
  ${KIWI_ROOT_DIR}/src/TagUtils.cpp
  ${KIWI_ROOT_DIR}/src/TypoTransformer.cpp
  ${KIWI_ROOT_DIR}/src/UnicodeCase.cpp
  ${KIWI_ROOT_DIR}/src/Utils.cpp
  ${KIWI_ROOT_DIR}/src/WordDetector.cpp
  ${KIWI_ROOT_DIR}/src/archImpl/none.cpp
)

# Third-party sources
file(GLOB STREAMVBYTE_SOURCES ${KIWI_ROOT_DIR}/third_party/streamvbyte/src/*.c)

# Find JNI
find_package(JNI REQUIRED)
include_directories(${JNI_INCLUDE_DIRS})

# Create the JNI library
add_library(kiwi-android SHARED
    ${KIWI_ROOT_DIR}/bindings/java/kiwi_java.cpp
    ${CORE_SRCS}
    ${KIWI_ROOT_DIR}/src/capi/kiwi_c.cpp
    ${STREAMVBYTE_SOURCES}
)

# Link against required libraries
target_link_libraries(kiwi-android log)

target_compile_features(kiwi-android PUBLIC cxx_std_17)

# Add compile definitions and flags
target_compile_definitions(kiwi-android PRIVATE 
    ANDROID=1
    KIWI_USE_MIMALLOC=0
    KIWI_USE_CPUINFO=0
    DNDEBUG
    DRELEASE
    KIWI_USE_BTREE
    DLL_EXPORT=1
)

target_compile_options(kiwi-android PRIVATE -O3)