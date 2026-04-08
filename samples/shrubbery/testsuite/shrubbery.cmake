# Arguments for testing shrubbery samples
macro(toolinvoke ARGS testfile outputdir)
  set(${ARGS} build ${testfile} -o ${outputdir}/ast.txt)
endmacro()

# Regular expression to match test files
set(TESTSUITE_REGEX ".*\\.shrubbery")

set(TESTSUITE_EXE "$<TARGET_FILE:shrubbery>")

function (test_output_dir out test)
  get_filename_component(test_dir ${test} DIRECTORY)
  get_filename_component(test_name ${test} NAME_WE)
  set(${out} "${test_dir}/${test_name}_out" PARENT_SCOPE)
endfunction()
