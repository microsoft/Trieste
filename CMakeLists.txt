cmake_minimum_required(VERSION 3.14.0)

file(STRINGS "VERSION" TRIESTE_VERSION_FILE LIMIT_COUNT 1)
string(STRIP "${TRIESTE_VERSION_FILE}" TRIESTE_VERSION_FILE)
if(NOT TRIESTE_VERSION_FILE MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR
    "VERSION file must contain a semantic version (X.Y.Z), got: '${TRIESTE_VERSION_FILE}'")
endif()
string(REPLACE "." ";" TRIESTE_VERSION_LIST ${TRIESTE_VERSION_FILE})
list(GET TRIESTE_VERSION_LIST 0 TRIESTE_VERSION_MAJOR)
list(GET TRIESTE_VERSION_LIST 1 TRIESTE_VERSION_MINOR)
list(GET TRIESTE_VERSION_LIST 2 TRIESTE_VERSION_REVISION)
set(TRIESTE_VERSION ${TRIESTE_VERSION_MAJOR}.${TRIESTE_VERSION_MINOR}.${TRIESTE_VERSION_REVISION})

project(trieste VERSION ${TRIESTE_VERSION} LANGUAGES CXX)

# #############################################
# Version info
find_package(Git QUIET)

if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -1 --format=%H%n%h%n%aD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE TRIESTE_GIT_LOG_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  string(REPLACE "\n" ";" TRIESTE_GIT_LOG_LIST "${TRIESTE_GIT_LOG_OUTPUT}")
  list(LENGTH TRIESTE_GIT_LOG_LIST TRIESTE_GIT_LOG_LEN)
  if(TRIESTE_GIT_LOG_LEN EQUAL 3)
    list(GET TRIESTE_GIT_LOG_LIST 0 TRIESTE_GIT_HASH)
    list(GET TRIESTE_GIT_LOG_LIST 1 TRIESTE_GIT_SHORT_HASH)
    list(GET TRIESTE_GIT_LOG_LIST 2 TRIESTE_BUILD_DATE)
  else()
    set(TRIESTE_GIT_HASH "unknown")
    set(TRIESTE_GIT_SHORT_HASH "unknown")
    set(TRIESTE_BUILD_DATE "unknown")
  endif()
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE TRIESTE_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
else()
  message(STATUS "Git not found; version metadata will be 'unknown'")
  set(TRIESTE_GIT_HASH "unknown")
  set(TRIESTE_GIT_BRANCH "unknown")
  set(TRIESTE_GIT_SHORT_HASH "unknown")
  set(TRIESTE_BUILD_DATE "unknown")
endif()

set(TRIESTE_BUILD_TOOLCHAIN "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/trieste/version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/trieste/version.h"
  @ONLY
)

# #############################################
# Options
option(TRIESTE_ENABLE_TESTING "Specifies whether to enable Trieste's tests" OFF)
option(TRIESTE_BUILD_SAMPLES "Specifies whether to build the samples" ON)
option(TRIESTE_BUILD_PARSERS "Specifies whether to build the parsers" ON)
option(TRIESTE_BUILD_PARSER_TESTS "Specifies whether to build the parser tests" OFF)
option(TRIESTE_BUILD_PARSER_TOOLS "Specifies whether to build parser tools" OFF)
option(TRIESTE_BUILD_REGEX_BENCHMARK "Build regex engine benchmark against RE2 (opt-in; fetches RE2)" OFF)
option(TRIESTE_USE_CXX17 "Specifies whether to target the C++17 standard" OFF)
option(TRIESTE_CLEAN_INSTALL "Specifies whether to delete all files (recursively) from the install prefix before install" OFF)
option(TRIESTE_USE_SNMALLOC "Specifies that new/delete should be overridden with snmalloc" ON)
option(TRIESTE_USE_FETCH_CONTENT
  "Fetch snmalloc and CLI11 from git (ON) or locate via find_package (OFF). When OFF, consumers must have snmalloc and CLI11 findable by CMake (e.g. via vcpkg)."
  ON)

if(TRIESTE_BUILD_REGEX_BENCHMARK AND NOT TRIESTE_ENABLE_TESTING)
  message(FATAL_ERROR "TRIESTE_BUILD_REGEX_BENCHMARK requires TRIESTE_ENABLE_TESTING=ON.")
endif()

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

include(GNUInstallDirs)

# #############################################
# Dependencies

if(TRIESTE_USE_CXX17)
  set(CMAKE_CXX_STANDARD 17)
else()
  set(CMAKE_CXX_STANDARD 20)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TRIESTE_SANITIZE "" CACHE STRING "Argument to pass to sanitize (disabled by default)")

if(TRIESTE_USE_FETCH_CONTENT)
  include(FetchContent)

  set(SNMALLOC_BUILD_TESTING OFF CACHE INTERNAL "Turn off snmalloc tests")
  set(SNMALLOC_USE_CXX17 ${TRIESTE_USE_CXX17})

  # Used to provide
  #  FetchContent_MakeAvailable_ExcludeFromAll
  FetchContent_Declare(
      cmake_utils
      GIT_REPOSITORY https://github.com/mjp41/cmake_utils
      GIT_TAG 2bf98b5773ea7282197c823e205547d8c2e323c0
      GIT_SHALLOW FALSE
  )

  FetchContent_MakeAvailable(cmake_utils)

  FetchContent_Declare(
    snmalloc
    GIT_REPOSITORY https://github.com/microsoft/snmalloc
    GIT_TAG 1c7a93aad54198423944a786da9dad3962134f75
    # per https://gitlab.kitware.com/cmake/cmake/-/issues/17770, do not use GIT_SHALLOW here
    # or it will silently break new builds days later, once the commit hash above no longer
    # matches a named branch, like main.
    # Tracking a named branch with GIT_SHALLOW enabled is ok, however.
    # GIT_SHALLOW TRUE
  )

  FetchContent_MakeAvailable_ExcludeFromAll(snmalloc)

  FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11
    GIT_TAG 4160d259d961cd393fd8d67590a8c7d210207348
    GIT_SHALLOW TRUE
  )

  FetchContent_MakeAvailable_ExcludeFromAll(cli11)

  set(TRIESTE_SNMALLOC_TARGET snmalloc)
  set(TRIESTE_SNMALLOC_OVERRIDE_TARGET snmalloc-new-override)
else()
  find_package(snmalloc CONFIG REQUIRED)
  find_package(CLI11 CONFIG REQUIRED)

  set(TRIESTE_SNMALLOC_TARGET snmalloc::snmalloc)
  set(TRIESTE_SNMALLOC_OVERRIDE_TARGET "")
endif()

# #############################################
# Create target and set properties
add_library(trieste INTERFACE)

# Add an alias so that library can be used inside the build tree, e.g. when testing
add_library(trieste::trieste ALIAS trieste)

# Set target properties
target_include_directories(trieste
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(trieste
  INTERFACE
  CLI11::CLI11
  ${TRIESTE_SNMALLOC_TARGET}
)

if(TRIESTE_USE_SNMALLOC AND TRIESTE_SNMALLOC_OVERRIDE_TARGET)
  if(TRIESTE_SANITIZE)
    message(WARNING "Sanitizers are not supported with snmalloc, disabling snmalloc.")
  else()
    target_link_libraries(trieste INTERFACE ${TRIESTE_SNMALLOC_OVERRIDE_TARGET})
  endif()
endif()

if(TRIESTE_USE_CXX17)
  target_compile_definitions(trieste INTERFACE cxx_std_17 TRIESTE_USE_CXX17)
else()
  target_compile_definitions(trieste INTERFACE cxx_std_20)
endif()

if (TRIESTE_SANITIZE)
  target_compile_options(trieste INTERFACE -g -fsanitize=${TRIESTE_SANITIZE} -fno-omit-frame-pointer)
  target_link_libraries(trieste INTERFACE -fsanitize=${TRIESTE_SANITIZE})
endif()

function(enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /wd5030 /bigobj)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Werror -Wshadow)
  endif()
endfunction()

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
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR}/dist CACHE PATH "" FORCE)
endif()

if(TRIESTE_USE_FETCH_CONTENT)
  set(INSTALL_DEPS ${TRIESTE_SNMALLOC_TARGET} CLI11)
  if (TRIESTE_USE_SNMALLOC AND TRIESTE_SNMALLOC_OVERRIDE_TARGET)
    list(APPEND INSTALL_DEPS ${TRIESTE_SNMALLOC_OVERRIDE_TARGET})
  endif()
else()
  set(INSTALL_DEPS "")
endif()

install(TARGETS trieste ${INSTALL_DEPS}
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

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/trieste DESTINATION include
  PATTERN "*.in" EXCLUDE)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/trieste/version.h
  DESTINATION include/trieste)

# #############################################
# # Exporting from the build tree
export(EXPORT ${PROJECT_NAME}_Targets
  FILE ${CMAKE_CURRENT_BINARY_DIR}/triesteTargets.cmake
  NAMESPACE trieste::)

# Register package in the User Package Registry
export(PACKAGE trieste)

# #############################################
# # Formatting targets
if(TRIESTE_USE_FETCH_CONTENT)
  clangformat_targets(
    VERSION 18
    INCLUDE_PATTERNS include/*.h include/*.cc parsers/*.cc parsers/*.h samples/*.cc samples/*.h test/*.cc test/*.h
  )
endif()

# #############################################
# # Add core Trieste tests
if(TRIESTE_ENABLE_TESTING)
  enable_testing()
  add_subdirectory(test)
endif()

# #############################################
# # Add samples
if(TRIESTE_BUILD_SAMPLES)
  enable_testing()
  add_subdirectory(samples/infix)
  add_subdirectory(samples/shrubbery)
endif()

# #############################################
# # Add parsers
if(TRIESTE_BUILD_PARSERS)
  add_subdirectory(parsers)
endif()
