cmake_minimum_required(VERSION 3.14.0)
project(trieste VERSION 1.0.0 LANGUAGES CXX)

# #############################################
# Options
option(TRIESTE_BUILD_SAMPLES "Specifies whether to build the samples" ON)
option(TRIESTE_BUILD_PARSERS "Specifies whether to build the parsers" ON)
option(TRIESTE_BUILD_PARSER_TESTS "Specifies whether to build the parser tests" OFF)
option(TRIESTE_USE_CXX17 "Specifies whether to target the C++17 standard" OFF)
option(TRIESTE_CLEAN_INSTALL "Specifies whether to delete all files (recursively) from the install prefix before install" OFF)

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# #############################################
# Dependencies

include(FetchContent)

if(TRIESTE_USE_CXX17)
  set(CMAKE_CXX_STANDARD 17)
else()
  set(CMAKE_CXX_STANDARD 20)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SNMALLOC_BUILD_TESTING OFF CACHE INTERNAL "Turn off snmalloc tests")
if (MSVC)
  # Overriding malloc and free statically not supported on Windows.
else()
  set(SNMALLOC_STATIC_LIBRARY_PREFIX "")
endif()
set(SNMALLOC_USE_CXX17 ${TRIESTE_USE_CXX17})

set(RE2_BUILD_TESTING OFF CACHE INTERNAL "Turn off RE2 tests")

set(TRIESTE_SANITIZE "" CACHE STRING "Argument to pass to sanitize (disabled by default)")


FetchContent_Declare(
  snmalloc
  GIT_REPOSITORY https://github.com/microsoft/snmalloc
  GIT_TAG main
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(snmalloc)

FetchContent_Declare(
  re2
  GIT_REPOSITORY https://github.com/google/re2
  GIT_TAG 2022-12-01
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(re2)

FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11
  GIT_TAG b9be5b9444772324459989177108a6a65b8b2769
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(cli11)

# #############################################
# Create target and set properties
add_library(trieste INTERFACE)

# Add an alias so that library can be used inside the build tree, e.g. when testing
add_library(trieste::trieste ALIAS trieste)

# Set target properties
target_include_directories(trieste
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(trieste
  INTERFACE
  snmallocshim-static
  re2::re2
  CLI11::CLI11
)

if(TRIESTE_USE_CXX17)
  target_compile_definitions(trieste INTERFACE cxx_std_17)
else()
  target_compile_definitions(trieste INTERFACE cxx_std_20)
endif()

if (TRIESTE_SANITIZE)
  target_compile_options(trieste INTERFACE -g -fsanitize=${TRIESTE_SANITIZE} -fno-omit-frame-pointer)
  target_link_libraries(trieste INTERFACE -fsanitize=${TRIESTE_SANITIZE})
endif()

if(MSVC)
  target_compile_options(trieste INTERFACE /W4 /WX /wd5030 /bigobj)
else()
  target_compile_options(trieste INTERFACE
    -Wall -Wextra -Wpedantic -Werror -Wshadow)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(trieste INTERFACE -Wmismatched-tags -fstandalone-debug)
endif()

if(TRIESTE_CLEAN_INSTALL)
  message("${CMAKE_INSTALL_PREFIX} will be recursively cleaned before install")
  # Clear all existing files and folders from the install directory
  install(CODE [[
    file(REMOVE_RECURSE ${CMAKE_INSTALL_PREFIX}/.)
    ]])
endif()

# #############################################
# Installation instructions
set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR}/dist)

install(TARGETS trieste snmallocshim-static snmalloc re2 CLI11
  EXPORT ${PROJECT_NAME}_Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Create a ConfigVersion.cmake file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/triesteConfig.cmake.in
  ${PROJECT_BINARY_DIR}/triesteConfig.cmake
  INSTALL_DESTINATION
  ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake
)

install(EXPORT ${PROJECT_NAME}_Targets
  FILE ${PROJECT_NAME}Targets.cmake
  NAMESPACE ${PROJECT_NAME}::
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

install(FILES ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  ${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/trieste DESTINATION include)

# #############################################
# # Exporting from the build tree
export(EXPORT ${PROJECT_NAME}_Targets
  FILE ${CMAKE_CURRENT_BINARY_DIR}/triesteTargets.cmake
  NAMESPACE trieste::)

# Register package in the User Package Registry
export(PACKAGE trieste)

# #############################################
# # Add samples
if(TRIESTE_BUILD_SAMPLES)
  enable_testing()
  add_subdirectory(samples/infix)
endif()

# #############################################
# # Add parsers
if(TRIESTE_BUILD_PARSERS)
  add_subdirectory(parsers)
endif()
