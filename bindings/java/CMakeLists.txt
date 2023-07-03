find_package (Java REQUIRED)
find_package (JNI REQUIRED)
include (UseJava)

include_directories (
	${JNI_INCLUDE_DIRS}
)
add_library (KiwiJava SHARED kiwi_java.cpp
    $<TARGET_OBJECTS:${PROJECT_NAME}>
    $<TARGET_OBJECTS:clog>
    $<TARGET_OBJECTS:cpuinfo>
)
target_link_libraries (KiwiJava ${JAVA_JVM_LIBRARY})
target_compile_features(KiwiJava PUBLIC cxx_std_17)
add_jar (${PROJECT_NAME}-${PROJECT_VERSION} kr/pe/bab2min/Kiwi.java kr/pe/bab2min/KiwiBuilder.java ENTRY_POINT kr.pe.bab2min.Kiwi)
