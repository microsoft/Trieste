## JSON

add_executable(json_fuzzer json_fuzzer.cc)
enable_warnings(json_fuzzer)

target_link_libraries(json_fuzzer
  PRIVATE
  trieste::json)

add_executable(json_pattern_checker json_checker.cc)
enable_warnings(json_pattern_checker)

target_link_libraries(json_pattern_checker
  PRIVATE
  trieste::json)

add_executable(json_test json_test.cc)
enable_warnings(json_test)

target_link_libraries(json_test
  PRIVATE
  trieste::json)

add_executable(json_patch_test json_patch_test.cc)
enable_warnings(json_patch_test)

target_link_libraries(json_patch_test
  PRIVATE
  trieste::json)

set(JSON_TEST_SUITE_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../JSONTestSuite/test_parsing)
set(JSON_PATCH_TEST_SUITE_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../json-patch-tests/)

add_test(NAME json_fuzz_reader COMMAND json_fuzzer reader -f WORKING_DIRECTORY $<TARGET_FILE_DIR:json_fuzzer>)
add_test(NAME json_fuzz_reader_sequence COMMAND json_fuzzer reader -f --sequence WORKING_DIRECTORY $<TARGET_FILE_DIR:json_fuzzer>)
add_test(NAME json_fuzz_writer COMMAND json_fuzzer writer -f WORKING_DIRECTORY $<TARGET_FILE_DIR:json_fuzzer>)
add_test(NAME json_fuzz_writer_sequence COMMAND json_fuzzer writer -f --sequence WORKING_DIRECTORY $<TARGET_FILE_DIR:json_fuzzer>)
add_test(NAME json_check_patterns COMMAND json_pattern_checker -w -i json-value WORKING_DIRECTORY $<TARGET_FILE_DIR:json_pattern_checker>)
add_test(NAME json_test COMMAND json_test -wf ${JSON_TEST_SUITE_ROOT} WORKING_DIRECTORY $<TARGET_FILE_DIR:json_test>)
add_test(NAME json_test_manual COMMAND json_test -wf -n manual WORKING_DIRECTORY $<TARGET_FILE_DIR:json_test>)
add_test(NAME json_patch_tests COMMAND json_patch_test -f ${JSON_PATCH_TEST_SUITE_ROOT}/tests.json WORKING_DIRECTORY $<TARGET_FILE_DIR:json_patch_test>)
add_test(NAME json_patch_spec_tests COMMAND json_patch_test -f ${JSON_PATCH_TEST_SUITE_ROOT}/spec_tests.json WORKING_DIRECTORY $<TARGET_FILE_DIR:json_patch_test>)

install(TARGETS json_fuzzer json_test json_patch_test RUNTIME DESTINATION parsers)

## YAML

add_executable(yaml_fuzzer yaml_fuzzer.cc)
enable_warnings(yaml_fuzzer)

target_link_libraries(yaml_fuzzer
  PRIVATE
  trieste::yaml)

add_executable(yaml_pattern_checker yaml_checker.cc)
enable_warnings(yaml_pattern_checker)

target_link_libraries(yaml_pattern_checker
  PRIVATE
  trieste::yaml)

add_executable(yaml_test yaml_test.cc)
enable_warnings(yaml_test)

target_link_libraries(yaml_test
  PRIVATE
  trieste::yaml)

set(YAML_TEST_SUITE_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../yaml-test-suite)

add_test(NAME yaml_fuzz_reader COMMAND yaml_fuzzer reader -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_reader_sequence COMMAND yaml_fuzzer reader -f --sequence WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_writer COMMAND yaml_fuzzer writer -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_writer_sequence COMMAND yaml_fuzzer writer -f --sequence WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_event_writer COMMAND yaml_fuzzer event_writer -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_event_writer_sequence COMMAND yaml_fuzzer event_writer -f --sequence WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_fuzz_to_json COMMAND yaml_fuzzer to_json -f WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_fuzzer>)
add_test(NAME yaml_check_patterns COMMAND yaml_pattern_checker WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_pattern_checker>)
add_test(NAME yaml_test COMMAND yaml_test -wf ${YAML_TEST_SUITE_ROOT} WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_test>)
add_test(NAME yaml_test_crlf COMMAND yaml_test --crlf -wf ${YAML_TEST_SUITE_ROOT} WORKING_DIRECTORY $<TARGET_FILE_DIR:yaml_test>)

install(TARGETS yaml_fuzzer yaml_test RUNTIME DESTINATION parsers)
