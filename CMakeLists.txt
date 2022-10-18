cmake_minimum_required(VERSION 3.14.0)
project(trieste VERSION 1.0.0 LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(
  cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11
  GIT_TAG        b9be5b9444772324459989177108a6a65b8b2769
)

FetchContent_MakeAvailable(cli11)

##############################################
# Options

option( TRIESTE_BUILD_SAMPLES "Specifies whether to build the samples" ON )

set( CMAKE_BUILD_WITH_INSTALL_RPATH ON )

##############################################
# Create target and set properties

add_library(trieste INTERFACE)

#Add an alias so that library can be used inside the build tree, e.g. when testing
add_library(trieste::trieste ALIAS trieste)

#Set target properties
target_include_directories(trieste
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(trieste INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(trieste INTERFACE /W4 /WX /bigobj)
else()
  target_compile_options(trieste INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(trieste INTERFACE -Wmismatched-tags -fstandalone-debug)
endif()

##############################################
# Installation instructions

set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR}/dist)

# Clear all existing files and folders from the install directory
install(CODE [[
  file(REMOVE_RECURSE ${CMAKE_INSTALL_PREFIX})
  ]])

install(TARGETS trieste
  EXPORT ${PROJECT_NAME}_Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)  

#Create a ConfigVersion.cmake file
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

##############################################
## Exporting from the build tree
export(EXPORT ${PROJECT_NAME}_Targets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/triesteTargets.cmake
    NAMESPACE trieste::)

#Register package in the User Package Registry
export(PACKAGE trieste)

##############################################
## Add samples
if( TRIESTE_BUILD_SAMPLES )
  add_subdirectory(samples/verona)
endif()
