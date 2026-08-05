# Configure the standalone fixture used to test prerequisite failure handling.
cmake_minimum_required(VERSION 3.28)
project(testsuite_failure_propagation NONE)
enable_testing()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/testsuite.cmake")
testsuite(failure)
