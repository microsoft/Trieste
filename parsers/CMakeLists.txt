add_subdirectory(json)
add_subdirectory(yaml)

if(TRIESTE_BUILD_PARSER_TESTS)
    enable_testing()
    add_subdirectory(test)

    if(NOT IS_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/JSONTestSuite)
    execute_process(COMMAND ${GIT_EXECUTABLE} clone --depth=1 https://github.com/nst/JSONTestSuite
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                    OUTPUT_QUIET)
    endif()

    if(NOT IS_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/json-patch-tests)
    execute_process(COMMAND ${GIT_EXECUTABLE} clone --depth=1 https://github.com/json-patch/json-patch-tests
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                    OUTPUT_QUIET)
    endif()

    if(NOT IS_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/yaml-test-suite)
    execute_process(COMMAND ${GIT_EXECUTABLE} clone --depth=1 --branch data-2022-01-17 https://github.com/yaml/yaml-test-suite
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                    OUTPUT_QUIET)
    endif()
endif()
