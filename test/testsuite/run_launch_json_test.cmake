# Verify multi-suite launch/task generation and prerequisite execution semantics.

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(source "${WORK_DIR}/source")
set(binary "${WORK_DIR}/build")
set(markers "${WORK_DIR}/markers")
file(COPY
  "${CMAKE_CURRENT_LIST_DIR}/fixtures/launch_json/"
  DESTINATION "${source}")
file(MAKE_DIRECTORY
  "${markers}/one" "${markers}/two")
foreach(directory one two)
  file(COPY
    "${PROJECT_DIR}/cases.cmake"
    "${PROJECT_DIR}/input.case"
    DESTINATION "${source}/${directory}")
  foreach(node compile "prepare.debug+" middle other fail)
    file(MAKE_DIRECTORY "${source}/${directory}/${node}")
    file(WRITE "${source}/${directory}/${node}/exit_code.txt" "0")
    file(WRITE "${source}/${directory}/${node}/stdout.txt" "")
    file(WRITE "${source}/${directory}/${node}/stderr.txt" "")
  endforeach()
endforeach()
set(configure_command
  "${CMAKE_COMMAND}"
  -S "${source}"
  -B "${binary}"
  -DSCENARIO=valid
  "-DTESTSUITE_CMAKE=${TESTSUITE_CMAKE}"
  "-DMARKER_DIR=${markers}"
  -DTRIESTE_GENERATE_LAUNCH_JSON=ON)
find_program(NINJA_TOOL NAMES ninja)
if(NINJA_TOOL)
  list(APPEND configure_command -G "Ninja Multi-Config")
endif()
execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "Launch-JSON project did not configure.\n${stdout}\n${stderr}")
endif()

set(launch_json "${source}/.vscode/launch.json")
if(NOT EXISTS "${launch_json}")
  message(FATAL_ERROR "Named nodes did not generate launch.json.")
endif()
file(READ "${launch_json}" content)
foreach(
  name
  config-one/run
  config-one/compile
  config-one/dependent
  config-one/prepare.debug+
  config-one/middle
  config-one/other
  config-one/config
  config-one/fail
  config-one/fail-consumer
  config-one/0
  config-one/false-consumer
  config-two/run
  config-two/compile
  config-two/dependent
  config-two/prepare.debug+
  config-two/middle
  config-two/other
  config-two/config
  config-two/fail
  config-two/fail-consumer
  config-two/0
  config-two/false-consumer)
  string(FIND "${content}" "\"name\": \"${name}\"" name_position)
  if(name_position EQUAL -1)
    message(FATAL_ERROR
      "launch.json does not contain named node '${name}'.\n${content}")
  endif()
endforeach()
if(NINJA_TOOL)
  foreach(directory one two)
    if(NOT content MATCHES
      "\"program\": \"${source}/${directory}/debug-tool\"")
      message(FATAL_ERROR
        "Multi-config launch.json did not use the first configuration.\n"
        "${content}")
    endif()
    if(NOT content MATCHES
      "\"preLaunchTask\": \"config-${directory}/false-consumer: prerequisites\"")
      message(FATAL_ERROR
        "A false-valued dependency did not produce a preLaunchTask.\n${content}")
    endif()
  endforeach()
endif()

set(tasks_json "${source}/.vscode/tasks.json")
if(NOT EXISTS "${tasks_json}")
  message(FATAL_ERROR "Named dependencies did not generate tasks.json.")
endif()
file(READ "${tasks_json}" tasks)
foreach(directory one two)
  if(NOT tasks MATCHES
    "\"label\": \"config-${directory}/dependent: prerequisites\"")
    message(FATAL_ERROR
      "tasks.json lacks prerequisites for config-${directory}/dependent.\n"
      "${tasks}")
  endif()
  if(NOT content MATCHES
    "\"preLaunchTask\": \"config-${directory}/dependent: prerequisites\"")
    message(FATAL_ERROR
      "launch.json does not reference prerequisites for "
      "config-${directory}/dependent.\n${content}")
  endif()
endforeach()
if(content MATCHES "\"name\": \"config-one/run\"[^}]*preLaunchTask")
  message(FATAL_ERROR "A node without DEPENDS received a preLaunchTask.")
endif()
string(FIND
  "${tasks}" [[^(config-one/prepare\\.debug\\+)$]]
  escaped_pattern)
if(escaped_pattern EQUAL -1)
  message(FATAL_ERROR
    "Prerequisite task did not quote CTest regex metacharacters.\n${tasks}")
endif()
string(FIND
  "${tasks}" [[^(config-one/middle|config-one/other)$]]
  multiple_pattern)
if(multiple_pattern EQUAL -1)
  message(FATAL_ERROR
    "Prerequisite task did not include both direct dependencies.\n${tasks}")
endif()

set(ctest_prefix
  "${CMAKE_CTEST_COMMAND}"
  --test-dir "${binary}"
  --output-on-failure
  --no-tests=error)
if(NINJA_TOOL)
  list(APPEND ctest_prefix --build-config Debug)
endif()
execute_process(
  COMMAND ${ctest_prefix} --tests-regex "^config-one/compile$"
  RESULT_VARIABLE dependency_result
  OUTPUT_VARIABLE dependency_stdout
  ERROR_VARIABLE dependency_stderr)
if(NOT dependency_result EQUAL 0)
  message(FATAL_ERROR
    "Generated prerequisite task semantics failed.\n"
    "${dependency_stdout}\n${dependency_stderr}")
endif()
file(GLOB_RECURSE produced_artifacts
  "${binary}/one/testsuite-output/*/*/artifact.txt")
list(LENGTH produced_artifacts artifact_count)
if(NOT artifact_count EQUAL 1)
  message(FATAL_ERROR
    "Prerequisite CTest did not produce the compile artifact.")
endif()

file(REMOVE
  "${markers}/one/prepare"
  "${markers}/one/middle"
  "${markers}/one/other")
execute_process(
  COMMAND
    ${ctest_prefix}
    --tests-regex "^config-one/(middle|other)$"
  RESULT_VARIABLE transitive_result
  OUTPUT_VARIABLE transitive_stdout
  ERROR_VARIABLE transitive_stderr)
if(NOT transitive_result EQUAL 0)
  message(FATAL_ERROR
    "Multiple/transitive launch prerequisites failed.\n"
    "${transitive_stdout}\n${transitive_stderr}")
endif()
foreach(marker prepare middle other)
  if(NOT EXISTS "${markers}/one/${marker}")
    message(FATAL_ERROR
      "Launch prerequisites did not run '${marker}'.")
  endif()
endforeach()

file(REMOVE "${markers}/one/prepare")
set(meta_pattern [[^config-one/prepare\.debug\+$]])
execute_process(
  COMMAND ${ctest_prefix} --tests-regex "${meta_pattern}"
  RESULT_VARIABLE meta_result
  OUTPUT_VARIABLE meta_stdout
  ERROR_VARIABLE meta_stderr)
if(NOT meta_result EQUAL 0 OR NOT EXISTS "${markers}/one/prepare")
  message(FATAL_ERROR
    "Escaped prerequisite regex did not select its node.\n"
    "${meta_stdout}\n${meta_stderr}")
endif()

execute_process(
  COMMAND ${ctest_prefix} --tests-regex "^config-one/fail$"
  RESULT_VARIABLE failure_result
  OUTPUT_VARIABLE failure_stdout
  ERROR_VARIABLE failure_stderr)
if(failure_result EQUAL 0)
  message(FATAL_ERROR
    "A failing launch prerequisite did not block launch.\n"
    "${failure_stdout}\n${failure_stderr}")
endif()
