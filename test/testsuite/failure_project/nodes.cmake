# Create two kinds of failing prerequisite, their dependents, and an unrelated
# node so the driver can verify CTest fixture failure propagation.
set(TESTSUITE_REGEX "input\\.case$")
set(TESTSUITE_DEFINE define_nodes)

function(define_nodes input)
  testsuite_add_test(
    NAME mismatch
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo actual)

  testsuite_add_test(
    NAME after-mismatch
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS mismatch
    COMMAND "${CMAKE_COMMAND}" -E touch "${MARKER_DIR}/after-mismatch")

  testsuite_add_test(
    NAME broken
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/missing-executable")

  testsuite_add_test(
    NAME after-broken
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS broken
    COMMAND "${CMAKE_COMMAND}" -E touch "${MARKER_DIR}/after-broken")

  testsuite_add_test(
    NAME independent
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${MARKER_DIR}/independent")
endfunction()
