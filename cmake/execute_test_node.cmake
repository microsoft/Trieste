# Unified named-node executor.
#
# Configure-time code writes immutable metadata and command arguments to the
# generated CMake file named by NODE_CONFIG_FILE. Both VERIFY and UPDATE load
# that file and follow the same pipeline; only the final phase differs.

# testsuite_add_test() writes scalars and the TESTSUITE_GOLDENS and
# TESTSUITE_ARTIFACTS lists directly. Only command arguments are indexed,
# because their generator expressions have not yet been evaluated on the
# writing side. After file(GENERATE) evaluates them, this is the first point
# where empty or list-valued results can be detected and rejected.

# Reconstruct and validate the command arguments stored in the configuration.
function(config_command out)
  if(NOT TESTSUITE_COMMAND_COUNT GREATER 0)
    message(FATAL_ERROR "Test node requires a command.")
  endif()

  set(command)
  math(EXPR last "${TESTSUITE_COMMAND_COUNT} - 1")
  foreach(index RANGE 0 ${last})
    set(argument "${TESTSUITE_COMMAND_${index}}")
    if(argument STREQUAL "" OR argument MATCHES ";")
      message(FATAL_ERROR
        "Command argument '${argument}' is unsupported by CMake.")
    endif()
    list(APPEND command "${argument}")
  endforeach()
  set(${out} "${command}" PARENT_SCOPE)
endfunction()

# Require a path to identify an existing non-directory file.
function(require_file kind path)
  if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    message(FATAL_ERROR "${kind} '${path}' does not exist or is not a file.")
  endif()
endfunction()

# Require every declared golden and artifact to exist in the output directory.
function(validate_generated output_dir goldens artifacts)
  foreach(relative IN LISTS goldens)
    require_file("Generated golden file" "${output_dir}/${relative}")
  endforeach()
  foreach(relative IN LISTS artifacts)
    require_file("Generated artifact" "${output_dir}/${relative}")
  endforeach()
endfunction()

# Run the optional validator in a separate CMake process.
function(run_validator validator output_dir)
  if(validator STREQUAL "")
    return()
  endif()
  if(NOT EXISTS "${validator}" OR IS_DIRECTORY "${validator}")
    message(FATAL_ERROR "Validator '${validator}' does not exist.")
  endif()

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}"
      "-DOUTPUT_DIR:PATH=${output_dir}"
      -P "${validator}"
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Validator '${validator}' failed.")
  endif()
endfunction()

# Stage 1: load the node configuration file and recover the command without
# shell evaluation.
if(NOT DEFINED NODE_CONFIG_FILE OR NOT DEFINED MODE)
  message(FATAL_ERROR
    "execute_test_node.cmake requires NODE_CONFIG_FILE and MODE.")
endif()
if(NOT MODE STREQUAL "VERIFY" AND NOT MODE STREQUAL "UPDATE")
  message(FATAL_ERROR "Unknown test-node mode '${MODE}'.")
endif()
if(NOT EXISTS "${NODE_CONFIG_FILE}")
  message(FATAL_ERROR
    "Test-node configuration file '${NODE_CONFIG_FILE}' does not exist.")
endif()

include("${NODE_CONFIG_FILE}")
config_command(command)

# Stage 2: recreate the build-tree output directory and run the command while
# CMake owns stdout/stderr capture.
file(REMOVE_RECURSE "${TESTSUITE_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${TESTSUITE_OUTPUT_DIR}")
file(WRITE "${TESTSUITE_OUTPUT_DIR}/stdout.txt" "")
file(WRITE "${TESTSUITE_OUTPUT_DIR}/stderr.txt" "")

execute_process(
  COMMAND ${command}
  WORKING_DIRECTORY "${TESTSUITE_WORKING_DIRECTORY}"
  OUTPUT_FILE "${TESTSUITE_OUTPUT_DIR}/stdout.txt"
  ERROR_FILE "${TESTSUITE_OUTPUT_DIR}/stderr.txt"
  TIMEOUT "${TESTSUITE_TIMEOUT}"
  RESULT_VARIABLE status)

if(NOT status MATCHES "^-?[0-9]+$")
  message(FATAL_ERROR
    "Test command did not return a numeric exit code: ${status}")
endif()
file(WRITE "${TESTSUITE_OUTPUT_DIR}/exit_code.txt" "${status}")

# Stage 3: enforce the exact GOLDENS/ARTIFACTS allowlist around an isolated
# validator process. A validator cannot alter this script's local variables.
validate_generated(
  "${TESTSUITE_OUTPUT_DIR}" "${TESTSUITE_GOLDENS}" "${TESTSUITE_ARTIFACTS}")
run_validator(
  "${TESTSUITE_VALIDATOR}" "${TESTSUITE_OUTPUT_DIR}")
validate_generated(
  "${TESTSUITE_OUTPUT_DIR}" "${TESTSUITE_GOLDENS}" "${TESTSUITE_ARTIFACTS}")

# Stage 4: verification compares declared goldens; update copies generated
# files into the corresponding golden locations.
foreach(relative IN LISTS TESTSUITE_GOLDENS)
  set(golden "${TESTSUITE_GOLDEN_DIR}/${relative}")
  set(generated "${TESTSUITE_OUTPUT_DIR}/${relative}")
  if(MODE STREQUAL "VERIFY")
    require_file("Golden file" "${golden}")

    execute_process(
      COMMAND
        "${CMAKE_COMMAND}" -E compare_files --ignore-eol
        "${golden}" "${generated}"
      RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      if(NOT TESTSUITE_DIFF_TOOL STREQUAL "")
        execute_process(
          COMMAND "${TESTSUITE_DIFF_TOOL}" "${golden}" "${generated}")
      endif()
      message(FATAL_ERROR
        "Golden file '${golden}' differs from '${generated}'.")
    endif()
  else() # MODE is UPDATE.
    # GOLDENS may contain subdirectory paths, so create the full destination's
    # parent rather than only TESTSUITE_GOLDEN_DIR.
    cmake_path(GET golden PARENT_PATH golden_dir)
    file(MAKE_DIRECTORY "${golden_dir}")
    file(COPY_FILE
      "${generated}" "${golden}"
      ONLY_IF_DIFFERENT
      RESULT copy_result)
    if(NOT copy_result STREQUAL "0")
      message(FATAL_ERROR
        "Failed to update golden file '${golden}': ${copy_result}")
    endif()
  endif()
endforeach()
