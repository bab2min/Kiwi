################
# Define a test
add_executable(kiwi_test

bit_encode.cpp
test_c.cpp
test_cpp.cpp
)

######################################
# Configure the test to use GoogleTest
#
# If used often, could be made a macro.

add_dependencies(kiwi_test gtest)
include_directories(${source_dir}/googletest/include)
target_link_libraries(kiwi_test ${binary_dir}/googlemock/gtest/libgtest_main.a)
target_link_libraries(kiwi_test ${binary_dir}/googlemock/gtest/libgtest.a)
target_link_libraries(kiwi_test -Wl,--whole-archive -lpthread -Wl,--no-whole-archive)
target_link_libraries(kiwi_test "${CMAKE_PROJECT_NAME}_static")

##################################
# Just make the test runnable with
#   $ make test

enable_testing()
add_test(kiwi_test kiwi_test)