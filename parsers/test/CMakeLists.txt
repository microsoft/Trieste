## JSON

add_executable(json_fuzzer json_fuzzer.cc)

target_link_libraries(json_fuzzer
  PRIVATE
  trieste::json)

add_executable(json_test json_test.cc)

target_link_libraries(json_test
  PRIVATE
  trieste::json)

set(JSON_TEST_SUITE_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../JSONTestSuite/test_parsing)

add_test(NAME json_fuzz_reader COMMAND json_fuzzer reader -f WORKING_DIRECTORY $<TARGET_FILE_DIR:json_fuzzer>)
add_test(NAME json_fuzz_writer COMMAND json_fuzzer writer -f WORKING_DIRECTORY $<TARGET_FILE_DIR:json_fuzzer>)
add_test(NAME json_test COMMAND json_test -wf ${JSON_TEST_SUITE_ROOT} WORKING_DIRECTORY $<TARGET_FILE_DIR:json_test>)

install(TARGETS json_fuzzer json_test RUNTIME DESTINATION parsers)

## YAML

add_executable(yaml_fuzzer yaml_fuzzer.cc)

target_link_libraries(yaml_fuzzer
  PRIVATE
  trieste::yaml)

add_executable(yaml_test yaml_test.cc)

target_link_libraries(yaml_test
  PRIVATE
  trieste::yaml)

set(YAML_TEST_SUITE_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../yaml-test-suite)

add_test(NAME yaml_fuzz_reader COMMAND yaml_fuzzer reader -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_writer COMMAND yaml_fuzzer writer -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_event_writer COMMAND yaml_fuzzer event_writer -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_to_json COMMAND yaml_fuzzer to_json -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_test COMMAND yaml_test -wf ${YAML_TEST_SUITE_ROOT} WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_test>)
add_test(NAME yaml_test_crlf COMMAND yaml_test --crlf -wf ${YAML_TEST_SUITE_ROOT} WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_test>)

install(TARGETS yaml_fuzzer yaml_test RUNTIME DESTINATION parsers)
