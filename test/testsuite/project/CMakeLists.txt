# Configure one parameterized testsuite API validation scenario.
cmake_minimum_required(VERSION 3.28)
project(testsuite_config_test NONE)
enable_testing()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/testsuite.cmake")
testsuite(config)
