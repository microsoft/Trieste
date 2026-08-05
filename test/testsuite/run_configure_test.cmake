# Run one parameterized configure-time API validation scenario.

file(REMOVE_RECURSE "${BINARY_DIR}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    -Werror=dev
    -S "${SOURCE_DIR}"
    -B "${BINARY_DIR}"
    "-DSCENARIO=${SCENARIO}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

if(EXPECT_SUCCESS)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "Expected configuration to succeed, but it failed.\n${stdout}\n${stderr}")
  endif()
else()
  if(result EQUAL 0)
    message(FATAL_ERROR "Expected configuration to fail, but it succeeded.")
  endif()
  string(CONCAT output "${stdout}" "\n" "${stderr}")
  if(NOT output MATCHES "${EXPECT_MESSAGE}")
    message(FATAL_ERROR
      "Configuration failed without the expected diagnostic "
      "'${EXPECT_MESSAGE}'.\n${output}")
  endif()
endif()
