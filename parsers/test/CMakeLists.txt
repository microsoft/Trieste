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
