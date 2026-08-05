set(TESTSUITE_REGEX "\\.infix$")
set(TESTSUITE_DEFINE define_infix_test)

function(define_infix_test test)
  cmake_path(GET test PARENT_PATH test_dir)
  cmake_path(GET test FILENAME test_file)
  get_filename_component(stem "${test_file}" NAME_WE)
  set(node "${test_dir}/${stem}_out")

  testsuite_add_test(
    NAME "${node}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}"
    COMMAND "$<TARGET_FILE:infix>" "${test_file}")
endfunction()
