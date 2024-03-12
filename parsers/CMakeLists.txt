add_subdirectory(json)

if(TRIESTE_BUILD_PARSER_TESTS)
    enable_testing()
    add_subdirectory(test)

    if(NOT IS_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/JSONTestSuite)
    execute_process(COMMAND ${GIT_EXECUTABLE} clone --depth=1 https://github.com/nst/JSONTestSuite
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                    OUTPUT_QUIET)
    endif()
endif()
