## JSON

add_executable(json_trieste json_trieste.cc)

target_link_libraries(json_trieste
  PRIVATE
  trieste::json)

add_executable(json_test json_test.cc)

target_link_libraries(json_test
  PRIVATE
  trieste::json)

set(JSON_TEST_SUITE_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../JSONTestSuite/test_parsing)

add_test(NAME json_trieste COMMAND json_trieste test -f WORKING_DIRECTORY $<TARGET_FILE_DIR:json_trieste>)
add_test(NAME json_test COMMAND json_test -wf ${JSON_TEST_SUITE_ROOT} WORKING_DIRECTORY $<TARGET_FILE_DIR:json_test>)

install(TARGETS json_trieste json_test RUNTIME DESTINATION parsers)

## YAML

add_executable(yaml_trieste yaml_trieste.cc)

target_link_libraries(yaml_trieste
  PRIVATE
  trieste::yaml)

add_executable(yaml_test yaml_test.cc)

target_link_libraries(yaml_test
  PRIVATE
  trieste::yaml)

set(YAML_TEST_SUITE_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../yaml-test-suite)

add_test(NAME yaml_trieste COMMAND yaml_trieste test -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_trieste>)
add_test(NAME yaml_test COMMAND yaml_test -wf ${YAML_TEST_SUITE_ROOT} WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_test>)

install(TARGETS yaml_trieste yaml_test RUNTIME DESTINATION parsers)
