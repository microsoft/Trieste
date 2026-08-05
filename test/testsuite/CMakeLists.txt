# Register tests for named-node configuration, execution, updates, and launch
# integration.
set(testsuite_project "${CMAKE_CURRENT_SOURCE_DIR}/project")
set(testsuite_build_root "${CMAKE_CURRENT_BINARY_DIR}/project-build")

# Verify one CTest entry per node, artifact handoff, dependencies, and an
# argument whose value is also a testsuite keyword.
add_subdirectory(execution)

# Verify failed fixtures block dependents without blocking independent nodes.
add_test(
  NAME testsuite-fixtures-failure-propagation
  COMMAND
    ${CMAKE_COMMAND}
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/failure_project"
    "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/failure-project-build"
    "-DMARKER_DIR=${CMAKE_CURRENT_BINARY_DIR}/failure-markers"
    "-DTEST_CONFIG=$<CONFIG>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_failure_test.cmake")

# Verify semicolon-bearing command arguments are rejected at execution.
add_test(
  NAME testsuite-runner-rejects-semicolon
  COMMAND
    ${CMAKE_COMMAND}
    "-DEXECUTOR=${PROJECT_SOURCE_DIR}/cmake/execute_test_node.cmake"
    "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/semicolon-output"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_semicolon_test.cmake")

# Verify update ordering, output selection, validators, and failure blocking.
add_test(
  NAME testsuite-update-graph
  COMMAND
    ${CMAKE_COMMAND}
    "-DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/update_project"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/update-project"
    "-DTESTSUITE_CMAKE=${PROJECT_SOURCE_DIR}/cmake/testsuite.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_update_test.cmake")
set_tests_properties(testsuite-update-graph PROPERTIES RUN_SERIAL TRUE)

# Verify global and per-suite update targets across two suites.
add_test(
  NAME testsuite-multi-suite-update
  COMMAND
    ${CMAKE_COMMAND}
    "-DTESTSUITE_CMAKE=${PROJECT_SOURCE_DIR}/cmake/testsuite.cmake"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/multi-suite-update"
    "-DTEST_CONFIG=$<CONFIG>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_multi_suite_update_test.cmake")

# Verify launch/task JSON, multi-config paths, and prerequisite execution.
add_test(
  NAME testsuite-named-launch-json
  COMMAND
    ${CMAKE_COMMAND}
    "-DPROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/project"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/launch-json"
    "-DTESTSUITE_CMAKE=${PROJECT_SOURCE_DIR}/cmake/testsuite.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_launch_json_test.cmake")

function(add_testsuite_config_test name scenario succeeds message)
  add_test(
    NAME "testsuite-config-${name}"
    COMMAND
      ${CMAKE_COMMAND}
      "-DSOURCE_DIR=${testsuite_project}"
      "-DBINARY_DIR=${testsuite_build_root}/${name}"
      "-DSCENARIO=${scenario}"
      "-DEXPECT_SUCCESS=${succeeds}"
      "-DEXPECT_MESSAGE=${message}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_configure_test.cmake")
endfunction()

# Verify normal nodes, forward output references, artifacts, and dependencies.
add_testsuite_config_test(valid valid TRUE "")
# Reject duplicate node names within one suite.
add_testsuite_config_test(
  duplicate duplicate FALSE "Duplicate test node name")
# Reject dependencies that do not name registered nodes.
add_testsuite_config_test(
  unknown-dependency unknown-dependency FALSE "unknown dependency")
# Reject output paths referring to producers that were never registered.
add_testsuite_config_test(
  unknown-output unknown-output FALSE "output of unknown test node")
# Reject testsuite_output_path() calls without a producer node.
add_testsuite_config_test(
  missing-output-file missing-output-file FALSE "requires NODE")
# Reject cycles in the named-node dependency graph.
add_testsuite_config_test(
  cycle cycle FALSE "Dependency cycle")
# Reject node names that traverse outside the suite directory.
add_testsuite_config_test(
  unsafe-name unsafe-name FALSE "unsafe path component")
# Accept and canonicalize harmless relative path components.
add_testsuite_config_test(
  normalized-name normalized-name TRUE "")
# Reject generator expressions in configure-time graph identities.
add_testsuite_config_test(
  unsafe-generator-expression unsafe-generator-expression FALSE
  "unsupported path character")
# Accept names such as "0" that CMake otherwise treats as false.
add_testsuite_config_test(
  false-valued-name false-valued-name TRUE "")
# Keep parent/child logical names in independent runtime output directories.
add_testsuite_config_test(
  nested-output-isolation nested-output-isolation TRUE "")
# Reject collection files that do not provide a registration callback.
add_testsuite_config_test(
  missing-define missing-define FALSE "must set TESTSUITE_DEFINE")
