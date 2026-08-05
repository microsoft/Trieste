# Exercise artifact handoff, dependency fixtures, an independent node, and a
# command argument equal to the DEPENDS metadata keyword.
set(TESTSUITE_REGEX "input\\.case$")
set(TESTSUITE_DEFINE define_nodes)

function(define_nodes input)
  testsuite_output_path(
    artifact NODE pipeline/produce FILE artifact.txt)

  testsuite_add_test(
    NAME pipeline/produce
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    ARTIFACTS artifact.txt
    COMMAND
      "${CMAKE_COMMAND}"
      "-DOUTPUT_FILE=${artifact}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/write_artifact.cmake")

  testsuite_add_test(
    NAME pipeline/consume
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS pipeline/produce
    COMMAND "${CMAKE_COMMAND}" -E cat "${artifact}")

  testsuite_add_test(
    NAME independent
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo DEPENDS)
endfunction()
