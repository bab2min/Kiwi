if(NOT ANDROID)
    find_package (Java REQUIRED)
    find_package (JNI REQUIRED)
    include (UseJava)

    include_directories (
        ${JNI_INCLUDE_DIRS}
    )
    set(CMAKE_JAVA_COMPILE_FLAGS -source 8 -target 8 -encoding utf-8)
endif()

set(pkg_name "KiwiJava")
if(ANDROID)
    set(OBJECTS $<TARGET_OBJECTS:${PROJECT_NAME}_static> $<TARGET_OBJECTS:streamvbyte>)
else()
    set(OBJECTS $<TARGET_OBJECTS:${PROJECT_NAME}> $<TARGET_OBJECTS:streamvbyte>)
endif()

if(KIWI_USE_CPUINFO)
    list(APPEND OBJECTS $<TARGET_OBJECTS:cpuinfo>)
endif()

add_library (${pkg_name} SHARED csrc/kiwi_java.cpp
    ${OBJECTS}
)

if(ANDROID)
    target_compile_features(${pkg_name} PUBLIC cxx_std_17)

    # Add compile definitions and flags
    target_compile_definitions(${pkg_name} PRIVATE 
        ANDROID=1
    )

    target_compile_options(${pkg_name} PRIVATE -O3)
else()
    if(UNIX AND NOT APPLE)
        target_link_libraries( ${pkg_name}
            ${JAVA_JVM_LIBRARY}
            rt
        )
    else()
        target_link_libraries (${pkg_name} ${JAVA_JVM_LIBRARY})
    endif()
    target_compile_features(${pkg_name} PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(${pkg_name} PUBLIC
            /MT
        )
    endif()
    add_jar (${PROJECT_NAME}-java-${PROJECT_VERSION} src/kr/pe/bab2min/Kiwi.java src/kr/pe/bab2min/KiwiBuilder.java ENTRY_POINT kr.pe.bab2min.Kiwi)

    if(MSVC)
        add_custom_command(TARGET ${PROJECT_NAME}-java-${PROJECT_VERSION} POST_BUILD
            COMMAND mv $<TARGET_FILE:${pkg_name}> $<TARGET_FILE_NAME:${pkg_name}>
            COMMAND jar uf ${PROJECT_NAME}-java-${PROJECT_VERSION}.jar $<TARGET_FILE_NAME:${pkg_name}>
            DEPENDS ${PROJECT_NAME}-java-${PROJECT_VERSION}
        )
    else()
        add_custom_command(TARGET ${PROJECT_NAME}-java-${PROJECT_VERSION} POST_BUILD
            COMMAND jar uf ${PROJECT_NAME}-java-${PROJECT_VERSION}.jar $<TARGET_FILE_NAME:${pkg_name}>
            DEPENDS ${PROJECT_NAME}-java-${PROJECT_VERSION}
        )
    endif()
endif()