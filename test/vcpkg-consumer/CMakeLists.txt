cmake_minimum_required(VERSION 3.14)
project(trieste-vcpkg-test CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

find_package(trieste CONFIG REQUIRED)

add_executable(test-find-package test_find_package.cpp)
target_link_libraries(test-find-package PRIVATE trieste::trieste)
add_test(NAME find-package COMMAND test-find-package)

option(TEST_PARSERS "Test trieste::json and trieste::yaml targets" OFF)
if(TEST_PARSERS)
  add_executable(test-find-parsers test_find_parsers.cpp)
  target_link_libraries(test-find-parsers PRIVATE trieste::json trieste::yaml)
  add_test(NAME find-parsers COMMAND test-find-parsers)
endif()
