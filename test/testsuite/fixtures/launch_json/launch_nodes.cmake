# Exercise multi-config programs, regex metacharacters, multiple and transitive
# dependencies, failing prerequisites, and false-valued node names.
set(TESTSUITE_REGEX [[input\.case$]])
set(TESTSUITE_DEFINE define_config)

function(define_config input)
  testsuite_add_test(
    NAME [[prepare.debug+]]
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch
      "${MARKER_DIR}/${TESTSUITE_DIRECTORY}/prepare")
  testsuite_add_test(
    NAME middle
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS [[prepare.debug+]]
    COMMAND "${CMAKE_COMMAND}" -E touch
      "${MARKER_DIR}/${TESTSUITE_DIRECTORY}/middle")
  testsuite_add_test(
    NAME other
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch
      "${MARKER_DIR}/${TESTSUITE_DIRECTORY}/other")
  testsuite_add_test(
    NAME config
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS middle other
    COMMAND "$<TARGET_FILE:${TESTSUITE_TOOL}>")
  testsuite_add_test(
    NAME fail
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E false)
  testsuite_add_test(
    NAME fail-consumer
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS fail
    COMMAND "$<TARGET_FILE:${TESTSUITE_TOOL}>")
  testsuite_add_test(
    NAME 0
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E true)
  testsuite_add_test(
    NAME false-consumer
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS 0
    COMMAND "$<TARGET_FILE:${TESTSUITE_TOOL}>")
endfunction()
