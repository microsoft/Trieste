# Testsuite architecture
# ----------------------
# testsuite() is the configure-time entry point. It discovers collection files,
# whose callbacks call testsuite_add_test() to register DAG nodes.
#
# Named registration emits a per-node configuration file, one CTest test, and
# one update target. After every collection has run, testsuite() validates the
# complete graph, wires update-target dependencies, and creates suite/global
# targets. execute_test_node.cmake is the single runtime path for named
# verification and updates.
#
# Suite and node metadata live in namespaced GLOBAL properties because
# collection callbacks execute in nested function scopes and dependencies may
# refer to nodes declared later.

find_program(DIFF_TOOL NAMES
  diff)

if (DIFF_TOOL STREQUAL DIFF_TOOL-NOTFOUND)
  set(DIFF_TOOL "")
endif()

# Convert an API path to one stable relative spelling. Semicolons are CMake
# list separators, backslashes are platform-dependent, and generator
# expressions are evaluated too late for graph identities and source paths.
function(_testsuite_normalize_relative_path out kind value)
  if(value STREQUAL "")
    message(FATAL_ERROR "${kind} must not be empty.")
  endif()
  if(value MATCHES [[\\|;|\$<]])
    message(FATAL_ERROR "${kind} '${value}' contains an unsupported path character.")
  endif()

  set(path "${value}")
  cmake_path(IS_ABSOLUTE path is_absolute)
  if(is_absolute)
    message(FATAL_ERROR "${kind} '${value}' must be relative.")
  endif()
  cmake_path(NORMAL_PATH path OUTPUT_VARIABLE normalized)
  if(normalized STREQUAL "." OR normalized MATCHES "^\\.\\.(/|$)")
    message(FATAL_ERROR "${kind} '${value}' contains an unsafe path component.")
  endif()

  set(${out} "${normalized}" PARENT_SCOPE)
endfunction()

# Recover the suite currently invoking a collection callback.
function(_testsuite_active_suite out_key out_name out_source out_binary)
  get_property(key GLOBAL PROPERTY TRIESTE_TESTSUITE_ACTIVE_KEY)
  if(NOT key)
    message(FATAL_ERROR
      "testsuite_add_test() and testsuite_output_path() may only be called "
      "while testsuite() is processing a collection.")
  endif()

  get_property(name GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_NAME")
  get_property(source GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_SOURCE_DIR")
  get_property(binary GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_BINARY_DIR")
  set(${out_key} "${key}" PARENT_SCOPE)
  set(${out_name} "${name}" PARENT_SCOPE)
  set(${out_source} "${source}" PARENT_SCOPE)
  set(${out_binary} "${binary}" PARENT_SCOPE)
endfunction()

# Public named-node helpers.

# Return a producer's deterministic build-tree path and record the reference so
# graph validation can ensure the producer exists.
function(testsuite_output_path out)
  cmake_parse_arguments(PATH "" "NODE;FILE" "" ${ARGN})
  if(
    PATH_UNPARSED_ARGUMENTS OR
    PATH_KEYWORDS_MISSING_VALUES OR
    NOT DEFINED PATH_NODE OR
    PATH_NODE STREQUAL "")
    message(FATAL_ERROR
      "testsuite_output_path() requires NODE and accepts an optional FILE.")
  endif()

  _testsuite_active_suite(key suite source binary)
  _testsuite_normalize_relative_path(node "Test node name" "${PATH_NODE}")
  set_property(
    GLOBAL APPEND PROPERTY
    "TRIESTE_TESTSUITE_${key}_OUTPUT_REFERENCES" "${node}")
  get_property(output_root GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_OUTPUT_ROOT")
  string(SHA256 node_key "${key}|${node}")
  set(path "${output_root}/${node_key}")

  if(DEFINED PATH_FILE AND NOT PATH_FILE STREQUAL "")
    _testsuite_normalize_relative_path(file "Test output file" "${PATH_FILE}")
    set(path "${path}/${file}")
  endif()

  set(${out} "${path}" PARENT_SCOPE)
endfunction()

# Configuration-file and JSON serialization helpers.

# Write one CMake configuration-file assignment using a bracket delimiter that
# does not occur in the value.
function(_testsuite_config_assignment out name value)
  set(equals)
  while(TRUE)
    string(FIND "${value}" "]${equals}]" closing)
    if(closing EQUAL -1)
      break()
    endif()
    string(APPEND equals "=")
  endwhile()
  set(${out} "set(${name} [${equals}[${value}]${equals}])\n" PARENT_SCOPE)
endfunction()

# Escape a scalar before inserting it into generated JSON.
function(_testsuite_json_escape out value)
  string(REPLACE "\\" "\\\\" escaped "${value}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  string(REPLACE "\n" "\\n" escaped "${escaped}")
  set(${out} "${escaped}" PARENT_SCOPE)
endfunction()

# Escape a literal CTest name for use in --tests-regex.
function(_testsuite_regex_escape out value)
  set(metacharacters "." "^" "$" "*" "+" "?" "(" ")" "[" "]" "{" "}" "|")
  string(LENGTH "${value}" length)
  set(escaped)
  if(length GREATER 0)
    math(EXPR last "${length} - 1")
    foreach(index RANGE 0 ${last})
      string(SUBSTRING "${value}" ${index} 1 character)
      if(character IN_LIST metacharacters)
        string(APPEND escaped "\\")
      endif()
      string(APPEND escaped "${character}")
    endforeach()
  endif()
  set(${out} "${escaped}" PARENT_SCOPE)
endfunction()

# Register one named DAG node and emit all of its configure-time products.
function(testsuite_add_test)
  # COMMAND is parsed manually so it can remain an opaque argv tail rather
  # than being reinterpreted as testsuite metadata.
  if(ARGC EQUAL 0)
    message(FATAL_ERROR "testsuite_add_test() requires arguments.")
  endif()

  math(EXPR last_argument "${ARGC} - 1")
  foreach(index RANGE 0 ${last_argument})
    string(FIND "${ARGV${index}}" ";" semicolon)
    if(NOT semicolon EQUAL -1)
      message(FATAL_ERROR
        "testsuite_add_test() arguments must not contain semicolons.")
    endif()
  endforeach()

  list(FIND ARGV "COMMAND" command_index)
  if(command_index LESS 0 OR command_index EQUAL last_argument)
    message(FATAL_ERROR
      "testsuite_add_test() requires COMMAND as its final metadata keyword, "
      "followed by at least one command argument.")
  endif()

  list(SUBLIST ARGV 0 ${command_index} metadata)
  math(EXPR command_start "${command_index} + 1")
  list(SUBLIST ARGV ${command_start} -1 command)

  cmake_parse_arguments(
    NODE
    ""
    "NAME;WORKING_DIRECTORY;TIMEOUT;VALIDATOR"
    "DEPENDS;GOLDENS;ARTIFACTS"
    ${metadata})
  if(NODE_UNPARSED_ARGUMENTS OR NODE_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR
      "Invalid testsuite_add_test() metadata: "
      "${NODE_UNPARSED_ARGUMENTS}${NODE_KEYWORDS_MISSING_VALUES}")
  endif()
  if(NOT DEFINED NODE_NAME OR NODE_NAME STREQUAL "")
    message(FATAL_ERROR "testsuite_add_test() requires NAME.")
  endif()
  if(
    NOT DEFINED NODE_WORKING_DIRECTORY OR
    NODE_WORKING_DIRECTORY STREQUAL "")
    message(FATAL_ERROR
      "testsuite_add_test(NAME '${NODE_NAME}') requires WORKING_DIRECTORY.")
  endif()
  if(NOT DEFINED NODE_TIMEOUT OR NODE_TIMEOUT STREQUAL "")
    set(NODE_TIMEOUT 20)
  elseif(NOT "${NODE_TIMEOUT}" MATCHES "^[1-9][0-9]*([.][0-9]+)?$")
    message(FATAL_ERROR
      "Test node '${NODE_NAME}' TIMEOUT must be a positive number.")
  endif()

  # Normalize all source/build paths before recording ownership. This keeps
  # later graph and runtime checks independent of collection spelling.
  _testsuite_active_suite(key suite source binary)
  if(source STREQUAL binary)
    message(FATAL_ERROR
      "Named testsuite nodes require an out-of-source CMake build.")
  endif()
  _testsuite_normalize_relative_path(name "Test node name" "${NODE_NAME}")

  get_property(nodes GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_NODES")
  if(name IN_LIST nodes)
    message(FATAL_ERROR "Duplicate test node name '${name}' in suite '${suite}'.")
  endif()

  if(NOT DEFINED NODE_GOLDENS OR NODE_GOLDENS STREQUAL "")
    set(NODE_GOLDENS exit_code.txt stdout.txt stderr.txt)
  endif()
  set(normalized_goldens)
  set(normalized_artifacts)
  set(normalized_dependencies)
  foreach(golden IN LISTS NODE_GOLDENS)
    _testsuite_normalize_relative_path(
      normalized_golden "GOLDENS entry" "${golden}")
    list(APPEND normalized_goldens "${normalized_golden}")
  endforeach()
  list(REMOVE_DUPLICATES normalized_goldens)
  if(NOT "exit_code.txt" IN_LIST normalized_goldens)
    message(FATAL_ERROR
      "Test node '${name}' GOLDENS must include exit_code.txt.")
  endif()

  foreach(artifact IN LISTS NODE_ARTIFACTS)
    _testsuite_normalize_relative_path(
      normalized_artifact "ARTIFACTS entry" "${artifact}")
    list(APPEND normalized_artifacts "${normalized_artifact}")
  endforeach()
  list(REMOVE_DUPLICATES normalized_artifacts)
  foreach(artifact IN LISTS normalized_artifacts)
    if(artifact IN_LIST normalized_goldens)
      message(FATAL_ERROR
        "Test node '${name}' declares '${artifact}' as both a GOLDEN "
        "and an ARTIFACT.")
    endif()
  endforeach()

  foreach(dependency IN LISTS NODE_DEPENDS)
    _testsuite_normalize_relative_path(
      normalized_dependency "Dependency name" "${dependency}")
    list(APPEND normalized_dependencies "${normalized_dependency}")
  endforeach()
  list(REMOVE_DUPLICATES normalized_dependencies)

  string(SHA256 node_key "${key}|${name}")
  get_property(output_root GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_OUTPUT_ROOT")
  set(golden_dir "${source}/${name}")
  # Physical directories use node keys so logical names such as foo and
  # foo/bar cannot recursively delete one another's runtime output.
  set(output_dir "${output_root}/${node_key}")
  file(MAKE_DIRECTORY "${output_dir}")

  # Store only graph metadata globally. The generated node configuration file
  # carries execution metadata shared by CTest and update targets.
  set_property(GLOBAL APPEND PROPERTY "TRIESTE_TESTSUITE_${key}_NODES" "${name}")
  set_property(
    GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_${node_key}_DEPENDS"
    "${normalized_dependencies}")

  # Generate one file per build configuration so generator expressions are
  # resolved without duplicating serialization between verify and update.
  # execute_test_node.cmake includes this file directly: scalar and list values
  # are complete variables. Command arguments remain indexed because their
  # generator expressions cannot be checked until file(GENERATE) evaluates
  # them; the executor then rejects empty or list-valued results.
  set(config_file_content)
  foreach(
    assignment
    "TESTSUITE_OUTPUT_DIR;${output_dir}"
    "TESTSUITE_GOLDEN_DIR;${golden_dir}"
    "TESTSUITE_WORKING_DIRECTORY;${NODE_WORKING_DIRECTORY}"
    "TESTSUITE_TIMEOUT;${NODE_TIMEOUT}"
    "TESTSUITE_VALIDATOR;${NODE_VALIDATOR}"
    "TESTSUITE_DIFF_TOOL;${DIFF_TOOL}")
    list(POP_FRONT assignment assignment_name)
    string(JOIN ";" assignment_value ${assignment})
    _testsuite_config_assignment(
      config_line "${assignment_name}" "${assignment_value}")
    string(APPEND config_file_content "${config_line}")
  endforeach()

  _testsuite_config_assignment(
    config_line "TESTSUITE_GOLDENS" "${normalized_goldens}")
  string(APPEND config_file_content "${config_line}")
  _testsuite_config_assignment(
    config_line "TESTSUITE_ARTIFACTS" "${normalized_artifacts}")
  string(APPEND config_file_content "${config_line}")

  list(LENGTH command command_count)
  string(APPEND config_file_content
    "set(TESTSUITE_COMMAND_COUNT ${command_count})\n")
  set(index 0)
  foreach(argument IN LISTS command)
    _testsuite_config_assignment(
      config_line "TESTSUITE_COMMAND_${index}" "${argument}")
    string(APPEND config_file_content "${config_line}")
    math(EXPR index "${index} + 1")
  endforeach()

  set(config_dir "${binary}/CMakeFiles/trieste-testsuite/${key}")
  file(MAKE_DIRECTORY "${config_dir}")
  set(config_file "${config_dir}/${node_key}-$<CONFIG>.cmake")
  file(GENERATE OUTPUT "${config_file}" CONTENT "${config_file_content}")

  # The node's single public CTest entry both executes and verifies. Fixtures
  # make selected leaves pull in prerequisites and stop after failed setup.
  set(public_test "${suite}/${name}")
  set(verified_fixture "__testsuite_${node_key}_verified")
  set(required_fixtures)
  foreach(dependency IN LISTS normalized_dependencies)
    string(SHA256 dependency_key "${key}|${dependency}")
    list(APPEND required_fixtures "__testsuite_${dependency_key}_verified")
  endforeach()

  add_test(
    NAME "${public_test}"
    COMMAND
      ${CMAKE_COMMAND}
      "-DNODE_CONFIG_FILE=${config_file}"
      -DMODE=VERIFY
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/execute_test_node.cmake")
  set_tests_properties(
    "${public_test}" PROPERTIES
    FIXTURES_SETUP "${verified_fixture}"
    LABELS "${suite}")
  if(required_fixtures)
    set_tests_properties(
      "${public_test}" PROPERTIES
      FIXTURES_REQUIRED "${required_fixtures}")
  endif()

  # Update targets use the same executor and node configuration file.
  # Dependency edges are added once collections have declared the full graph.
  set(update_target "__testsuite_update_${node_key}")
  set(command_target_dependencies)
  set(index 0)
  foreach(argument IN LISTS command)
    if(argument MATCHES "\\$<TARGET_")
      # Repeating target-valued arguments on the custom target lets CMake add
      # build dependencies that are otherwise hidden in the generated file.
      list(APPEND command_target_dependencies
        "-DTESTSUITE_COMMAND_TARGET_${index}:STRING=${argument}")
    endif()
    math(EXPR index "${index} + 1")
  endforeach()
  add_custom_target(
    "${update_target}"
    COMMAND
      ${CMAKE_COMMAND}
      ${command_target_dependencies}
      "-DNODE_CONFIG_FILE=${config_file}"
      -DMODE=UPDATE
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/execute_test_node.cmake"
    VERBATIM)
  set_property(
    GLOBAL APPEND PROPERTY
    "TRIESTE_TESTSUITE_${key}_UPDATE_TARGETS" "${update_target}")

  # Launch entries and dependency tasks are accumulated now and written once
  # after all suites have contributed, avoiding duplicate file(GENERATE)
  # outputs.
  set(pre_launch_property)
  list(LENGTH normalized_dependencies dependency_count)
  if(dependency_count GREATER 0)
    set(task_label "${suite}/${name}: prerequisites")
    _testsuite_json_escape(launch_task_label "${task_label}")
    set(dependency_patterns)
    foreach(dependency IN LISTS normalized_dependencies)
      _testsuite_regex_escape(
        dependency_pattern "${suite}/${dependency}")
      list(APPEND dependency_patterns "${dependency_pattern}")
    endforeach()
    string(JOIN "|" dependency_pattern ${dependency_patterns})
    _testsuite_json_escape(
      launch_dependency_pattern "^(${dependency_pattern})$")
    _testsuite_json_escape(launch_ctest "${CMAKE_CTEST_COMMAND}")
    _testsuite_json_escape(launch_binary_dir "${CMAKE_BINARY_DIR}")

    get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    set(config_arguments)
    if(is_multi_config)
      set(config_arguments
        "        \"--build-config\", \"$<CONFIG>\",\n")
    elseif(NOT CMAKE_BUILD_TYPE STREQUAL "")
      _testsuite_json_escape(launch_config "${CMAKE_BUILD_TYPE}")
      set(config_arguments
        "        \"--build-config\", \"${launch_config}\",\n")
    endif()

    set(task_entry
      "    {
      \"label\": \"${launch_task_label}\",
      \"type\": \"process\",
      \"command\": \"${launch_ctest}\",
      \"args\": [
        \"--test-dir\", \"${launch_binary_dir}\",
${config_arguments}        \"--output-on-failure\",
        \"--no-tests=error\",
        \"--tests-regex\", \"${launch_dependency_pattern}\"
      ],
      \"problemMatcher\": []
    },")
    set_property(
      GLOBAL APPEND PROPERTY
      "TRIESTE_TESTSUITE_${key}_VSCODE_TASKS" "${task_entry}")
    set(pre_launch_property
      "      \"preLaunchTask\": \"${launch_task_label}\",\n")
  endif()

  list(GET command 0 launch_program)
  list(REMOVE_AT command 0)
  _testsuite_json_escape(launch_name "${suite}/${name}")
  _testsuite_json_escape(launch_program "${launch_program}")
  _testsuite_json_escape(launch_cwd "${NODE_WORKING_DIRECTORY}")
  set(launch_args)
  foreach(argument IN LISTS command)
    _testsuite_json_escape(escaped_argument "${argument}")
    list(APPEND launch_args "\"${escaped_argument}\"")
  endforeach()
  string(JOIN ", " launch_args ${launch_args})
  set(launch_entry
    "    {
      \"name\": \"${launch_name}\",
      \"type\": \"cppdbg\",
      \"request\": \"launch\",
${pre_launch_property}      \"program\": \"${launch_program}\",
      \"args\": [${launch_args}],
      \"stopAtEntry\": false,
      \"cwd\": \"${launch_cwd}\"
    },")
  set_property(
    GLOBAL APPEND PROPERTY
    "TRIESTE_TESTSUITE_${key}_LAUNCH_JSON" "${launch_entry}")
endfunction()

# Named graph validation and dependency emission.

# Depth-first traversal validates dependency names and detects cycles.
function(_testsuite_validate_node key suite nodes node path)
  string(SHA256 node_key "${key}|${node}")
  get_property(
    state GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_${node_key}_STATE")
  if(state STREQUAL "DONE")
    return()
  endif()
  if(state STREQUAL "VISITING")
    list(FIND path "${node}" cycle_start)
    list(SUBLIST path ${cycle_start} -1 cycle)
    list(APPEND cycle "${node}")
    string(JOIN " -> " cycle_message ${cycle})
    message(FATAL_ERROR
      "Dependency cycle in suite '${suite}': ${cycle_message}")
  endif()
  set_property(
    GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_${node_key}_STATE" VISITING)

  get_property(
    dependencies GLOBAL PROPERTY
    "TRIESTE_TESTSUITE_${key}_${node_key}_DEPENDS")

  set(next_path ${path} "${node}")
  foreach(dependency IN LISTS dependencies)
    if(NOT dependency IN_LIST nodes)
      message(FATAL_ERROR
        "Test node '${node}' in suite '${suite}' has unknown dependency "
        "'${dependency}'.")
    endif()
    _testsuite_validate_node(
      "${key}" "${suite}" "${nodes}" "${dependency}" "${next_path}")
  endforeach()

  set_property(
    GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_${node_key}_STATE" DONE)
endfunction()

# Validate every node after all collections have registered forward references.
function(_testsuite_validate_graph key suite)
  get_property(nodes GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_NODES")
  get_property(
    output_references GLOBAL PROPERTY
    "TRIESTE_TESTSUITE_${key}_OUTPUT_REFERENCES")
  foreach(reference IN LISTS output_references)
    if(NOT reference IN_LIST nodes)
      message(FATAL_ERROR
        "Suite '${suite}' references the output of unknown test node "
        "'${reference}'.")
    endif()
  endforeach()
  foreach(node IN LISTS nodes)
    _testsuite_validate_node("${key}" "${suite}" "${nodes}" "${node}" "")
  endforeach()
endfunction()

# Mirror validated DAG edges onto the per-node golden update targets.
function(_testsuite_wire_named_updates key)
  get_property(nodes GLOBAL PROPERTY "TRIESTE_TESTSUITE_${key}_NODES")
  foreach(node IN LISTS nodes)
    string(SHA256 node_key "${key}|${node}")
    set(update_target "__testsuite_update_${node_key}")
    get_property(
      dependencies GLOBAL PROPERTY
      "TRIESTE_TESTSUITE_${key}_${node_key}_DEPENDS")
    foreach(dependency IN LISTS dependencies)
      string(SHA256 dependency_key "${key}|${dependency}")
      add_dependencies(
        "${update_target}" "__testsuite_update_${dependency_key}")
    endforeach()
  endforeach()
endfunction()

# Deferred project-wide output for entries accumulated by every suite.
# Write launch and task files once, after every suite has contributed entries.
function(_testsuite_generate_vscode_files)
  get_property(entries GLOBAL PROPERTY TRIESTE_TESTSUITE_LAUNCH_JSON)
  string(REPLACE ";" "\n" entries "${entries}")
  set(launch_content
"{
  \"version\": \"0.2.0\",
  \"configurations\": [
${entries}
  ]
}")
  get_property(tasks GLOBAL PROPERTY TRIESTE_TESTSUITE_VSCODE_TASKS)
  string(REPLACE ";" "\n" tasks "${tasks}")
  set(tasks_content
"{
  \"version\": \"2.0.0\",
  \"tasks\": [
${tasks}
  ]
}")

  get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  set(condition)
  if(is_multi_config)
    list(GET CMAKE_CONFIGURATION_TYPES 0 launch_configuration)
    set(condition CONDITION "$<CONFIG:${launch_configuration}>")
  endif()
  file(GENERATE
    OUTPUT "${CMAKE_SOURCE_DIR}/.vscode/launch.json"
    CONTENT "${launch_content}"
    ${condition})
  file(GENERATE
    OUTPUT "${CMAKE_SOURCE_DIR}/.vscode/tasks.json"
    CONTENT "${tasks_content}"
    ${condition})
endfunction()

# Public suite entry point.
#
# How to use this testsuite system.
# In a directory with the testsuite files, create a CMakeLists.txt file,
# include this file, and call testsuite() with a suite name:
#
#   include (${CMAKE_SOURCE_DIR}/cmake/testsuite.cmake)
#   testsuite(infix)
#
# testsuite() loads each adjacent .cmake collection. A collection selects
# candidates with TESTSUITE_REGEX.
#
# TESTSUITE_DEFINE names a callback which registers nodes with
# testsuite_add_test(). Nodes have suite-local NAMEs, COMMANDs, exact GOLDENS,
# transient ARTIFACTS, and optional DEPENDS edges. Use
# testsuite_output_path() to pass a producer's generated artifact to a
# dependent node. See docs/testsuite.md for the complete API.
function(testsuite name)
  if(TARGET "${name}-update-dump")
    message(FATAL_ERROR "testsuite(${name}) called more than once. "
                        "Remove duplicate or use a different name.")
  endif()
  message(STATUS "Building test suite: ${name}")

  get_property(active_key GLOBAL PROPERTY TRIESTE_TESTSUITE_ACTIVE_KEY)
  if(active_key)
    message(FATAL_ERROR "Nested testsuite() calls are not supported.")
  endif()
  string(SHA256 suite_key
    "${CMAKE_CURRENT_SOURCE_DIR}|${CMAKE_CURRENT_BINARY_DIR}|${name}")
  file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" suite_source_dir)
  file(REAL_PATH "${CMAKE_CURRENT_BINARY_DIR}" suite_binary_dir)

  # Initialize the suite registry before including collections. Named
  # callbacks use TRIESTE_TESTSUITE_ACTIVE_KEY to find this state.
  set_property(GLOBAL PROPERTY TRIESTE_TESTSUITE_ACTIVE_KEY "${suite_key}")
  set_property(GLOBAL PROPERTY "TRIESTE_TESTSUITE_${suite_key}_NAME" "${name}")
  set_property(
    GLOBAL PROPERTY "TRIESTE_TESTSUITE_${suite_key}_SOURCE_DIR"
    "${suite_source_dir}")
  set_property(
    GLOBAL PROPERTY "TRIESTE_TESTSUITE_${suite_key}_BINARY_DIR"
    "${suite_binary_dir}")
  set(output_root
    "${suite_binary_dir}/testsuite-output/${suite_key}")
  set_property(
    GLOBAL PROPERTY "TRIESTE_TESTSUITE_${suite_key}_OUTPUT_ROOT"
    "${output_root}")

  # Phase 1: discover candidates once, then let each collection select files
  # and register nodes through its TESTSUITE_DEFINE callback.
  file (GLOB test_collections CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cmake)
  file (GLOB_RECURSE all_files CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

  foreach(test_collection ${test_collections})
    unset(TESTSUITE_REGEX)
    unset(TESTSUITE_DEFINE)

    # Grab specific settings for this tool
    include(${CMAKE_CURRENT_SOURCE_DIR}/${test_collection})

    set (tests ${all_files})
    if(NOT DEFINED TESTSUITE_REGEX)
      message(FATAL_ERROR
        "Test collection '${test_collection}' must define TESTSUITE_REGEX.")
    endif()
    list(FILTER tests INCLUDE REGEX ${TESTSUITE_REGEX})

    if(NOT DEFINED TESTSUITE_DEFINE OR NOT COMMAND "${TESTSUITE_DEFINE}")
      message(FATAL_ERROR
        "Test collection '${test_collection}' must set TESTSUITE_DEFINE "
        "to a CMake command.")
    endif()
    foreach(test ${tests})
      cmake_language(CALL "${TESTSUITE_DEFINE}" "${test}")
    endforeach()
  endforeach()

  # Phase 2: all forward references now resolve, so validate the DAG and mirror
  # its edges onto update targets.
  _testsuite_validate_graph("${suite_key}" "${name}")
  _testsuite_wire_named_updates("${suite_key}")
  get_property(
    UPDATE_DUMPS_TARGETS GLOBAL PROPERTY
    "TRIESTE_TESTSUITE_${suite_key}_UPDATE_TARGETS")
  get_property(
    LAUNCH_JSON GLOBAL PROPERTY
    "TRIESTE_TESTSUITE_${suite_key}_LAUNCH_JSON")
  get_property(
    VSCODE_TASKS GLOBAL PROPERTY
    "TRIESTE_TESTSUITE_${suite_key}_VSCODE_TASKS")
  set_property(GLOBAL PROPERTY TRIESTE_TESTSUITE_ACTIVE_KEY "")

  # Phase 3: contribute launch entries and create per-suite public targets.
  if(TRIESTE_GENERATE_LAUNCH_JSON)
    set_property(
      GLOBAL APPEND PROPERTY TRIESTE_TESTSUITE_LAUNCH_JSON ${LAUNCH_JSON})
    set_property(
      GLOBAL APPEND PROPERTY TRIESTE_TESTSUITE_VSCODE_TASKS ${VSCODE_TASKS})
    get_property(
      launch_scheduled GLOBAL PROPERTY
      TRIESTE_TESTSUITE_LAUNCH_JSON_SCHEDULED)
    if(NOT launch_scheduled)
      set_property(
        GLOBAL PROPERTY TRIESTE_TESTSUITE_LAUNCH_JSON_SCHEDULED TRUE)
      cmake_language(
        DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
        CALL _testsuite_generate_vscode_files)
    endif()
  endif()


  add_custom_target("${name}-update-dump" DEPENDS ${UPDATE_DUMPS_TARGETS})

  # Phase 4: attach this suite to project-wide convenience targets.
  if (TARGET update-dump)
    add_dependencies(update-dump "${name}-update-dump")
  else()
    add_custom_target(update-dump DEPENDS "${name}-update-dump")
  endif()
endfunction()