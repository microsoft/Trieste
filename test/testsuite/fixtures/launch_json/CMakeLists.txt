# Configure two suites whose launch and task entries must share JSON files.
cmake_minimum_required(VERSION 3.28)
project(testsuite_launch_json NONE)
enable_testing()

add_subdirectory(one)
add_subdirectory(two)
