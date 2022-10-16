cmake_minimum_required(VERSION 3.10.0)

include_directories(src)
project(trieste CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/dist)

if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-Wmismatched-tags -fstandalone-debug)
endif()

add_subdirectory(external/CLI11 ./external/CLI11 EXCLUDE_FROM_ALL)
add_subdirectory(samples/verona)
