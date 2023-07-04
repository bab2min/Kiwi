find_package (Java REQUIRED)
find_package (JNI REQUIRED)
include (UseJava)

include_directories (
	${JNI_INCLUDE_DIRS}
)
set(pkg_name "KiwiJava-${PROJECT_VERSION}")
add_library (${pkg_name} SHARED kiwi_java.cpp
    $<TARGET_OBJECTS:${PROJECT_NAME}>
    $<TARGET_OBJECTS:clog>
    $<TARGET_OBJECTS:cpuinfo>
)
target_link_libraries (${pkg_name} ${JAVA_JVM_LIBRARY})
target_compile_features(${pkg_name} PUBLIC cxx_std_17)
add_jar (${PROJECT_NAME}-java-${PROJECT_VERSION} kr/pe/bab2min/Kiwi.java kr/pe/bab2min/KiwiBuilder.java ENTRY_POINT kr.pe.bab2min.Kiwi)

add_custom_command(TARGET ${PROJECT_NAME}-java-${PROJECT_VERSION} POST_BUILD
    COMMAND jar uf ${PROJECT_NAME}-java-${PROJECT_VERSION}.jar $<TARGET_FILE_NAME:${pkg_name}>
    DEPENDS ${PROJECT_NAME}-java-${PROJECT_VERSION}
)