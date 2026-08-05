set(TESTSUITE_REGEX "\\.shrubbery$")
set(TESTSUITE_DEFINE define_shrubbery_test)

function(define_shrubbery_test test)
  cmake_path(GET test PARENT_PATH test_dir)
  cmake_path(GET test FILENAME test_file)
  get_filename_component(stem "${test_file}" NAME_WE)
  set(node "${test_dir}/${stem}_out")
  testsuite_output_path(ast NODE "${node}" FILE ast.txt)

  testsuite_add_test(
    NAME "${node}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}"
    GOLDENS exit_code.txt stdout.txt stderr.txt ast.txt
    COMMAND
      "$<TARGET_FILE:shrubbery>" build "${test_file}" -o "${ast}")
endfunction()
