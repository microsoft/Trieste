# Define the configure-time API validation scenario selected by SCENARIO.
if(SCENARIO STREQUAL "missing-define")
  set(TESTSUITE_REGEX "input\\.case$")
else()
  set(TESTSUITE_REGEX "input\\.case$")
  set(TESTSUITE_DEFINE define_case)
endif()

function(add_node name)
  testsuite_add_test(
    NAME "${name}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E true)
endfunction()

function(define_case input)
  if(SCENARIO STREQUAL "valid")
    testsuite_output_path(compile_output NODE compile FILE artifact.txt)
    add_node(run)
    testsuite_add_test(
      NAME compile
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      ARTIFACTS artifact.txt
      COMMAND "${CMAKE_COMMAND}" -E touch "${compile_output}")
    testsuite_add_test(
      NAME dependent
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      DEPENDS compile
      COMMAND "${CMAKE_COMMAND}" -E true)
  elseif(SCENARIO STREQUAL "duplicate")
    add_node(duplicate)
    add_node(duplicate)
  elseif(SCENARIO STREQUAL "unknown-dependency")
    testsuite_add_test(
      NAME node
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      DEPENDS missing
      COMMAND "${CMAKE_COMMAND}" -E true)
  elseif(SCENARIO STREQUAL "unknown-output")
    testsuite_output_path(output NODE missing FILE artifact.txt)
    add_node(node)
  elseif(SCENARIO STREQUAL "missing-output-file")
    testsuite_output_path(output NODE node FILE)
  elseif(SCENARIO STREQUAL "cycle")
    testsuite_add_test(
      NAME first
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      DEPENDS second
      COMMAND "${CMAKE_COMMAND}" -E true)
    testsuite_add_test(
      NAME second
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      DEPENDS first
      COMMAND "${CMAKE_COMMAND}" -E true)
  elseif(SCENARIO STREQUAL "unsafe-name")
    add_node("../outside")
  elseif(SCENARIO STREQUAL "normalized-name")
    add_node("nested/../normalized")
  elseif(SCENARIO STREQUAL "unsafe-generator-expression")
    add_node("$<1:../outside>")
  elseif(SCENARIO STREQUAL "false-valued-name")
    add_node("0")
  elseif(SCENARIO STREQUAL "nested-output-isolation")
    testsuite_output_path(parent_output NODE parent)
    testsuite_output_path(child_output NODE parent/child)
    cmake_path(IS_PREFIX parent_output "${child_output}" NORMALIZE overlap)
    if(overlap)
      message(FATAL_ERROR "Nested node output directories overlap.")
    endif()
    add_node(parent)
    add_node(parent/child)
  else()
    message(FATAL_ERROR "Unknown SCENARIO '${SCENARIO}'.")
  endif()
endfunction()
