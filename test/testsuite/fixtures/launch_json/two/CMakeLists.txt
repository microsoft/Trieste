# Configure the second launch fixture with configuration-specific executables.
set(TESTSUITE_DIRECTORY two)
set(TESTSUITE_TOOL tool-two)

add_executable("${TESTSUITE_TOOL}" IMPORTED GLOBAL)
set_target_properties("${TESTSUITE_TOOL}" PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/default-tool"
  IMPORTED_LOCATION_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/debug-tool"
  IMPORTED_LOCATION_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/release-tool")

include("${TESTSUITE_CMAKE}")
testsuite(config-two)
