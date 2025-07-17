# Arguments for testing infix samples
macro(toolinvoke ARGS local_build testfile outputdir)
  set(${ARGS} "${local_build}/../infix${CMAKE_EXECUTABLE_SUFFIX}" ${testfile})
endmacro()

# Regular expression to match test files
# This regex matches files with the .infix extension
set(TESTSUITE_REGEX ".*\\.infix")

function (test_output_dir out test)
  # Use get_filename_component to remove the file extension and keep the directory structure
  get_filename_component(test_dir ${test} DIRECTORY)
  get_filename_component(test_name ${test} NAME_WE)
  # Create the output directory relative to the test directory
  set(${out} "${test_dir}/${test_name}_out" PARENT_SCOPE)
endfunction()
