find_program(DIFF_TOOL NAMES
  diff)

set(DIR_OF_TESTSUITE_CMAKE ${CMAKE_CURRENT_LIST_DIR})

if (DIFF_TOOL STREQUAL DIFF_TOOL-NOTFOUND)
  set(DIFF_TOOL "")
endif()

# How to use this testsuite system.
#  In a directory with the testsuite files, create a CMakeLists.txt file.
#  * Include this file.
#  * Call the testsuite function with the name of the tool.
# E.g. something like this:
#
#   include (${CMAKE_SOURCE_DIR}/cmake/testsuite.cmake)
#   testsuite(infix)
#
#  The testsuite function will find all adjacent .cmake files they should contain the following:
#  *  A variable TESTSUITE_REGEX, which specifies which files are to be considered a test.
#     This should be a regular expression that matches the test files. E.g. "test_type/.*\\.infix"
#     which matches all files with the .infix extension in the test_type directory.
#  *  A variable TESTSUITE_EXE which is the executable to run for the tests.  This can be a generator expression
#     to allow for different executables in different configurations.
#  *  A macro toolinvoke which takes the arguments ARGS, local_build, testfile and outputdir.
#     This macro should set ARGS to the command line arguments for the tool.
#  *  A function test_output_dir which takes the output directory out and the test file test.
#     This function should set out to the output directory for the test. This is relative to
#
# An example of this is in samples/infix/testsuite/infix.cmake.
function(testsuite name)
  message(STATUS "Building test suite: ${name}")
  # Iterate each tool
  set(UPDATE_DUMPS_TARGETS)
  # Each test collection has its own cmake file for its configuration.
  file (GLOB test_collections CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cmake)
  file (GLOB_RECURSE all_files CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

  set(GOLDEN_DIRS)

  foreach(test_collection ${test_collections})
    set (test_set)
    
    # Grab specific settings for this tool
    include(${CMAKE_CURRENT_SOURCE_DIR}/${test_collection})

    set (tests ${all_files})
    list(FILTER tests
      INCLUDE REGEX
      ${TESTSUITE_REGEX}
    )

    foreach(test ${tests})
      test_output_dir(output_dir_relative ${test})
      get_filename_component(test_dir ${test} DIRECTORY)
      get_filename_component(test_file ${test} NAME)
      # Create command to create the output for this test.
      set (output_dir ${CMAKE_CURRENT_BINARY_DIR}/${output_dir_relative})
      set (test_output_cmd 
        ${CMAKE_COMMAND}
          -DTESTFILE=${test_file}
          -DTEST_EXE=${TESTSUITE_EXE}
          -DWORKING_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}
          -DCOLLECTION=${CMAKE_CURRENT_SOURCE_DIR}/${test_collection}
          -DOUTPUT_DIR=${output_dir}
          -P ${DIR_OF_TESTSUITE_CMAKE}/runcommand.cmake
      )

      # Add test that rebuilds the compiler output
      add_test(NAME ${output_dir_relative}
        COMMAND ${test_output_cmd}
      )

      # Add command that rebuilts the compiler output for updating golden files.
      add_custom_command(OUTPUT "${output_dir_relative}_fake"
        COMMAND ${test_output_cmd}
      )
      set_source_files_properties("${output_dir_relative}_fake" PROPERTIES SYMBOLIC "true")
      list(APPEND test_set "${output_dir_relative}_fake")

      # Make json for debugging.
      toolinvoke(launch_json_args ${test_file} ${output_dir})
      # Convert to a json format list.
      string(REPLACE "\"" "\\\"" launch_json_args "${launch_json_args}")
      string(REPLACE ";" "\", \"" launch_json_args "${launch_json_args}")
      list(APPEND LAUNCH_JSON
  "    {
        \"name\": \"${output_dir_relative}\",
        \"type\": \"cppdbg\",
        \"request\": \"launch\",
        \"program\": \"${TESTSUITE_EXE}\",
        \"args\": [\"${launch_json_args}\"],
        \"stopAtEntry\": false,
        \"cwd\": \"${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}\",
      },")

      # Add output comparison for each golden / output file
      set (golden_dir  ${CMAKE_CURRENT_SOURCE_DIR}/${output_dir_relative} )
      list(APPEND GOLDEN_DIRS ${golden_dir})
      file (GLOB_RECURSE results CONFIGURE_DEPENDS RELATIVE ${golden_dir} ${golden_dir}/*)
      # Check if there are any files to compare for this test.
      list(LENGTH results res_length)
      if(res_length EQUAL 0)
        message(WARNING "Test does not have results directory: ${golden_dir}\nRun `update-dump` to generate golden files.")
        # Add to generate golden output target
        add_custom_command(OUTPUT ${output_dir_relative}_fake
          COMMAND
            ${CMAKE_COMMAND}
            -E make_directory
            ${golden_dir}
          APPEND
        )
        add_custom_command(OUTPUT ${output_dir_relative}_fake
          COMMAND
            ${CMAKE_COMMAND}
            -Dsrc=${output_dir}/*
            -Ddst=${golden_dir}/
            -P ${DIR_OF_TESTSUITE_CMAKE}/copy_if_different_and_exists.cmake
          APPEND
        )
      else()
        foreach (result ${results})
          # Check each file is correct as a test target
          add_test (NAME ${output_dir_relative}-${result}
            COMMAND 
              ${CMAKE_COMMAND}
                -Doriginal_file=${golden_dir}/${result} 
                -Dnew_file=${output_dir}/${result}
                -Ddiff_tool=${DIFF_TOOL}
                -P ${DIR_OF_TESTSUITE_CMAKE}/compare.cmake
          )
          set_tests_properties(${output_dir_relative}-${result} PROPERTIES DEPENDS ${output_dir_relative})

          # Override out of date files.
          add_custom_command(OUTPUT "${output_dir_relative}_fake"
            COMMAND
              ${CMAKE_COMMAND}
              -Dsrc=${output_dir}/${result}
              -Ddst=${golden_dir}/${result}
              -P ${DIR_OF_TESTSUITE_CMAKE}/copy_if_different_and_exists.cmake
            APPEND
          )
        endforeach()
        # All tests require an error_code.
        add_custom_command(OUTPUT "${output_dir_relative}_fake"
          COMMAND
            ${CMAKE_COMMAND}
            -Dsrc=${output_dir}/exit_code.txt
            -Ddst=${golden_dir}/exit_code.txt
            -P ${DIR_OF_TESTSUITE_CMAKE}/copy_if_different_and_exists.cmake
          APPEND
        )

      endif()
    endforeach()
    add_custom_target("update-dump-${test_collection}" DEPENDS ${test_set})
    list(APPEND UPDATE_DUMPS_TARGETS "update-dump-${test_collection}")
  endforeach()

  list(REMOVE_DUPLICATES GOLDEN_DIRS)

  string(REPLACE ";" "\n" LAUNCH_JSON2 "${LAUNCH_JSON}")

  if (TRIESTE_GENERATE_LAUNCH_JSON)
    file(GENERATE OUTPUT ${CMAKE_SOURCE_DIR}/.vscode/launch.json
      CONTENT
  "{
    \"version\": \"0.2.0\",
    \"configurations\": [
      ${LAUNCH_JSON2}
    ]
  }")
  endif()


  if (TARGET update-dump)
    add_dependencies(update-dump ${UPDATE_DUMPS_TARGETS})
  else()
    add_custom_target(update-dump DEPENDS ${UPDATE_DUMPS_TARGETS})
  endif()

  if(GOLDEN_DIRS)
    add_custom_target(update-dump-clean
      COMMAND ${CMAKE_COMMAND} -E remove_directory ${GOLDEN_DIRS}
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target update-dump
      USES_TERMINAL
    )
  else()
    add_custom_target(update-dump-clean
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target update-dump
      USES_TERMINAL
    )
  endif()
endfunction()