# Verify update DAG ordering, output allowlists, and validation.

function(configure_project source binary mode marker)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      -S "${source}"
      -B "${binary}"
      "-DMODE=${mode}"
      "-DMARKER=${marker}"
      "-DTESTSUITE_CMAKE=${TESTSUITE_CMAKE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "Update test project did not configure.\n${stdout}\n${stderr}")
  endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/success-source" "${WORK_DIR}/failure-source")
file(COPY "${PROJECT_DIR}/" DESTINATION "${WORK_DIR}/success-source")
file(COPY "${PROJECT_DIR}/" DESTINATION "${WORK_DIR}/failure-source")

set(success_source "${WORK_DIR}/success-source")
set(success_binary "${WORK_DIR}/success-build")
file(MAKE_DIRECTORY
  "${success_source}/produce"
  "${success_source}/consume")
file(WRITE "${success_source}/produce/stale.txt" "remove me")
configure_project(
  "${success_source}" "${success_binary}" success
  "${WORK_DIR}/unused-marker")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" --build "${success_binary}"
    --target update-graph-update-dump
    -j 4
  RESULT_VARIABLE success_result
  OUTPUT_VARIABLE success_stdout
  ERROR_VARIABLE success_stderr)
if(NOT success_result EQUAL 0)
  message(FATAL_ERROR
    "Successful update graph failed.\n${success_stdout}\n${success_stderr}")
endif()

foreach(
  golden
  produce/exit_code.txt
  produce/stdout.txt
  produce/stderr.txt
  consume/exit_code.txt
  consume/stdout.txt
  consume/stderr.txt)
  if(NOT EXISTS "${success_source}/${golden}")
    message(FATAL_ERROR "Update did not create '${golden}'.")
  endif()
endforeach()
file(READ "${success_source}/consume/stdout.txt" consumer_output)
if(NOT consumer_output STREQUAL "payload\nvalidated\n")
  message(FATAL_ERROR
    "Dependent update did not consume the producer artifact.")
endif()
if(
  EXISTS "${success_source}/produce/artifact.txt" OR
  EXISTS "${success_source}/produce/extra.txt")
  message(FATAL_ERROR
    "Transient or unlisted output was copied into the golden directory.")
endif()
if(NOT EXISTS "${success_source}/produce/stale.txt")
  message(FATAL_ERROR
    "Clean update removed an undeclared file.")
endif()
execute_process(
  COMMAND
    "${CMAKE_CTEST_COMMAND}"
    --test-dir "${success_binary}"
    --output-on-failure
    -j 4
  RESULT_VARIABLE verify_result
  OUTPUT_VARIABLE verify_stdout
  ERROR_VARIABLE verify_stderr)
if(NOT verify_result EQUAL 0)
  message(FATAL_ERROR
    "Updated goldens did not verify.\n${verify_stdout}\n${verify_stderr}")
endif()

set(failure_source "${WORK_DIR}/failure-source")
set(failure_binary "${WORK_DIR}/failure-build")
set(failure_marker "${WORK_DIR}/dependent-ran")
configure_project(
  "${failure_source}" "${failure_binary}" failure "${failure_marker}")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" --build "${failure_binary}"
    --target update-graph-update-dump
    -j 4
  RESULT_VARIABLE failure_result
  OUTPUT_VARIABLE failure_stdout
  ERROR_VARIABLE failure_stderr)
if(failure_result EQUAL 0)
  message(FATAL_ERROR "Update graph with a missing artifact passed.")
endif()
if(EXISTS "${failure_marker}")
  message(FATAL_ERROR
    "Dependent update ran after its prerequisite failed validation.")
endif()
string(CONCAT failure_output "${failure_stdout}" "\n" "${failure_stderr}")
if(NOT failure_output MATCHES "Generated artifact")
  message(FATAL_ERROR
    "Missing artifact diagnostic was not reported.\n${failure_output}")
endif()
