
add_executable(trieste_intrusive_ptr_test
  intrusive_ptr_test.cc
)
enable_warnings(trieste_intrusive_ptr_test)
target_link_libraries(trieste_intrusive_ptr_test trieste::trieste)

# This test might not make so much sense without asan enabled, but might as well
# check that the test compiles and doesn't crash on other compilers.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT TRIESTE_SANITIZE)
  target_compile_options(trieste_intrusive_ptr_test PUBLIC -g -fsanitize=thread)
  target_link_libraries(trieste_intrusive_ptr_test -fsanitize=thread)
endif()

add_test(NAME trieste_intrusive_ptr_test COMMAND trieste_intrusive_ptr_test WORKING_DIRECTORY $<TARGET_FILE_DIR:trieste_intrusive_ptr_test>)

add_executable(trieste_source_test
  source_test.cc
)
enable_warnings(trieste_source_test)
target_link_libraries(trieste_source_test trieste::trieste)

add_test(NAME trieste_source_test COMMAND trieste_source_test --depth 6 WORKING_DIRECTORY $<TARGET_FILE_DIR:trieste_source_test>)
