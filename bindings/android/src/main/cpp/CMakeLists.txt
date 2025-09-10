# Create the JNI library
set(KIWI_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../)

add_library(kiwi-android SHARED
    ${KIWI_ROOT_DIR}/bindings/java/kiwi_java.cpp
    $<TARGET_OBJECTS:${PROJECT_NAME}_static>
    $<TARGET_OBJECTS:streamvbyte>
)

target_compile_features(kiwi-android PUBLIC cxx_std_17)

# Add compile definitions and flags
target_compile_definitions(kiwi-android PRIVATE 
    ANDROID=1
)

target_compile_options(kiwi-android PRIVATE -O3)