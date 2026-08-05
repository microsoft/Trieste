# Configure two suites to test global and suite-specific update targets.
cmake_minimum_required(VERSION 3.28)
project(multi_suite_update NONE)
enable_testing()

add_subdirectory(one)
add_subdirectory(two)
