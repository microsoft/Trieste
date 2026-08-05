# Verify aggregate updates run every suite and per-suite updates remain isolated.

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/source")

set(source "${WORK_DIR}/source")
set(binary "${WORK_DIR}/build")
set(build_config_args)
set(ctest_config_args)
if(NOT TEST_CONFIG STREQUAL "")
  list(APPEND build_config_args --config "${TEST_CONFIG}")
  list(APPEND ctest_config_args --build-config "${TEST_CONFIG}")
endif()

file(COPY
  "${CMAKE_CURRENT_LIST_DIR}/fixtures/multi_suite_update/"
  DESTINATION "${source}")

foreach(suite one two)
  file(MAKE_DIRECTORY "${source}/${suite}/result")
  file(WRITE "${source}/${suite}/input.test" "")
  file(WRITE "${source}/${suite}/result/exit_code.txt" "0")
  file(WRITE "${source}/${suite}/result/stdout.txt" "")
  file(WRITE "${source}/${suite}/result/stderr.txt" "")
endforeach()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    -S "${source}"
    -B "${binary}"
    "-DTESTSUITE_CMAKE=${TESTSUITE_CMAKE}"
    "-DMARKER_DIR=${WORK_DIR}"
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_stdout
  ERROR_VARIABLE configure_stderr)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
    "Multi-suite update project did not configure.\n"
    "${configure_stdout}\n${configure_stderr}")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" --build "${binary}"
    --target update-dump
    ${build_config_args}
    -j 4
  RESULT_VARIABLE update_result
  OUTPUT_VARIABLE update_stdout
  ERROR_VARIABLE update_stderr)
if(NOT update_result EQUAL 0)
  message(FATAL_ERROR
    "Parallel aggregate update failed.\n"
    "${update_stdout}\n${update_stderr}")
endif()

file(REMOVE "${WORK_DIR}/one-ran" "${WORK_DIR}/two-ran")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" --build "${binary}"
    --target two-update-dump
    ${build_config_args}
  RESULT_VARIABLE suite_update_result
  OUTPUT_VARIABLE suite_update_stdout
  ERROR_VARIABLE suite_update_stderr)
if(NOT suite_update_result EQUAL 0)
  message(FATAL_ERROR
    "Per-suite update failed.\n"
    "${suite_update_stdout}\n${suite_update_stderr}")
endif()
if(EXISTS "${WORK_DIR}/one-ran" OR NOT EXISTS "${WORK_DIR}/two-ran")
  message(FATAL_ERROR
    "Per-suite update target updated an unrelated suite.")
endif()

execute_process(
  COMMAND
    "${CMAKE_CTEST_COMMAND}"
    --test-dir "${binary}"
    --output-on-failure
    ${ctest_config_args}
    -j 2
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_stdout
  ERROR_VARIABLE test_stderr)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR
    "Multi-suite goldens did not verify after update.\n"
    "${test_stdout}\n${test_stderr}")
endif()
