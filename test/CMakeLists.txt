
add_executable(trieste_intrusive_ptr_test
  intrusive_ptr_test.cc
)
target_link_libraries(trieste_intrusive_ptr_test trieste::trieste)

# This test might not make so much sense without asan enabled, but might as well
# check that the test compiles and doesn't crash on other compilers.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(trieste_intrusive_ptr_test PUBLIC -g -fsanitize=thread)
  target_link_libraries(trieste_intrusive_ptr_test -fsanitize=thread)
endif()

add_test(NAME trieste_intrusive_ptr_test COMMAND trieste_intrusive_ptr_test)
