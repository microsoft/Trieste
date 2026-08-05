# Mark execution of the first suite's update node.
set(TESTSUITE_REGEX [[input\.test$]])
set(TESTSUITE_DEFINE define_test)

function(define_test input)
  testsuite_add_test(
    NAME result
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${MARKER_DIR}/one-ran")
endfunction()
