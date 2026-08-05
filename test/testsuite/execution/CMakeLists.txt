# Register the execution fixture and require exactly one CTest entry per node.
include("${PROJECT_SOURCE_DIR}/cmake/testsuite.cmake")

testsuite(node-execution)

get_property(node_tests DIRECTORY PROPERTY TESTS)
list(LENGTH node_tests node_test_count)
if(NOT node_test_count EQUAL 3)
  message(FATAL_ERROR
    "Expected one CTest entry for each of 3 nodes, got ${node_test_count}.")
endif()
