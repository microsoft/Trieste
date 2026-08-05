# Verify failed setup nodes skip dependents while unrelated graph branches run.

file(REMOVE_RECURSE "${BINARY_DIR}" "${MARKER_DIR}")
file(MAKE_DIRECTORY "${MARKER_DIR}")

set(ctest_config_args)
if(NOT TEST_CONFIG STREQUAL "")
  list(APPEND ctest_config_args --build-config "${TEST_CONFIG}")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    -S "${SOURCE_DIR}"
    -B "${BINARY_DIR}"
    "-DMARKER_DIR=${MARKER_DIR}"
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_stdout
  ERROR_VARIABLE configure_stderr)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "Failure-propagation project did not configure.\n"
    "${configure_stdout}\n${configure_stderr}")
endif()

execute_process(
  COMMAND
    "${CMAKE_CTEST_COMMAND}"
    --test-dir "${BINARY_DIR}"
    --output-on-failure
    ${ctest_config_args}
    -j 4
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_stdout
  ERROR_VARIABLE test_stderr)
if(test_result EQUAL 0)
  message(FATAL_ERROR
    "Failure-propagation project unexpectedly passed.\n${test_stdout}")
endif()

if(EXISTS "${MARKER_DIR}/after-mismatch")
  message(FATAL_ERROR "Dependent after golden mismatch was executed.")
endif()
if(EXISTS "${MARKER_DIR}/after-broken")
  message(FATAL_ERROR "Dependent after launch failure was executed.")
endif()
if(NOT EXISTS "${MARKER_DIR}/independent")
  message(FATAL_ERROR "Independent node was not executed.")
endif()

string(CONCAT output "${test_stdout}" "\n" "${test_stderr}")
if(NOT output MATCHES "differs from")
  message(FATAL_ERROR
    "Golden mismatch diagnostic was not reported.\n${output}")
endif()
if(NOT output MATCHES "did not return a numeric exit code")
  message(FATAL_ERROR
    "Launch failure diagnostic was not reported.\n${output}")
endif()
if(NOT output MATCHES "Not Run")
  message(FATAL_ERROR
    "Skipped dependent tests were not reported as Not Run.\n${output}")
endif()
