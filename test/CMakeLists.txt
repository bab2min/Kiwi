################
# Define a test
add_executable(kiwi-test

bit_encode.cpp
test_QEncoder.cpp
test_typo.cpp
test_combiner.cpp
test_c.cpp
test_cpp.cpp
test_sw_tokenizer.cpp
)

######################################
# Configure the test to use GoogleTest
#
# If used often, could be made a macro.

add_dependencies(kiwi-test gtest)
include_directories(third_party/googletest/include)
target_link_libraries(kiwi-test gtest_main gtest)
if(MSVC)
  
else()
  #target_link_libraries(kiwi-test -Wl,--whole-archive -lpthread -Wl,--no-whole-archive)
endif()
target_link_libraries(kiwi-test "${CMAKE_PROJECT_NAME}_static")

##################################
# Just make the test runnable with
#   $ make test

enable_testing()
add_test(kiwi-test kiwi-test)