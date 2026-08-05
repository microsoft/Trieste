# Verify the executor rejects semicolon-bearing command arguments.

file(REMOVE_RECURSE "${OUTPUT_DIR}")
set(config_file "${OUTPUT_DIR}-config.cmake")
file(WRITE "${config_file}"
  "set(TESTSUITE_OUTPUT_DIR [[${OUTPUT_DIR}]])\n"
  "set(TESTSUITE_GOLDEN_DIR [[${OUTPUT_DIR}]])\n"
  "set(TESTSUITE_WORKING_DIRECTORY [[${CMAKE_CURRENT_LIST_DIR}]])\n"
  "set(TESTSUITE_TIMEOUT 20)\n"
  "set(TESTSUITE_VALIDATOR [[]])\n"
  "set(TESTSUITE_DIFF_TOOL [[]])\n"
  "set(TESTSUITE_GOLDENS [[]])\n"
  "set(TESTSUITE_ARTIFACTS [[]])\n"
  "set(TESTSUITE_COMMAND_COUNT 4)\n"
  "set(TESTSUITE_COMMAND_0 [[${CMAKE_COMMAND}]])\n"
  "set(TESTSUITE_COMMAND_1 -E)\n"
  "set(TESTSUITE_COMMAND_2 echo)\n"
  "set(TESTSUITE_COMMAND_3 [[a;b]])\n")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DNODE_CONFIG_FILE=${config_file}"
    -DMODE=VERIFY
    -P "${EXECUTOR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(result EQUAL 0)
  message(FATAL_ERROR "Executor accepted a semicolon-bearing argument.")
endif()

string(CONCAT output "${stdout}" "\n" "${stderr}")
if(NOT output MATCHES "unsupported by CMake")
  message(FATAL_ERROR
    "Runner failed without the expected diagnostic.\n${output}")
endif()
