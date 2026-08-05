# In success mode, exercise artifact consumption and validation; in failure
# mode, omit a declared artifact and verify that its dependent does not run.
set(TESTSUITE_REGEX "input\\.case$")
set(TESTSUITE_DEFINE define_nodes)

function(define_nodes input)
  testsuite_output_path(artifact NODE produce FILE artifact.txt)

  if(MODE STREQUAL "success")
    testsuite_add_test(
      NAME produce
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      ARTIFACTS artifact.txt
      COMMAND "$<TARGET_FILE:update_producer>" "${artifact}")

    testsuite_add_test(
      NAME consume
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      DEPENDS produce
      VALIDATOR "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validator.cmake"
      COMMAND "${CMAKE_COMMAND}" -E cat "${artifact}")
  elseif(MODE STREQUAL "failure")
    testsuite_add_test(
      NAME produce
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      ARTIFACTS artifact.txt
      COMMAND "${CMAKE_COMMAND}" -E true)

    testsuite_add_test(
      NAME consume
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      DEPENDS produce
      COMMAND "${CMAKE_COMMAND}" -E touch "${MARKER}")
  else()
    message(FATAL_ERROR "Unknown MODE '${MODE}'.")
  endif()
endfunction()
