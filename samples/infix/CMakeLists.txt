add_executable(infix_trieste
  reader.cc
  infix_trieste.cc
  parse.cc
  )

target_link_libraries(infix_trieste
  trieste::trieste
)

add_executable(infix_test
  reader.cc
  parse.cc
  writers.cc
  infix_test.cc
)

target_link_libraries(infix_test
  trieste::trieste
)
 
add_test(NAME infix COMMAND infix_trieste test -f)
add_test(NAME invalid_input COMMAND infix ./infix)
set_property(TEST invalid_input PROPERTY WILL_FAIL On)
add_test(NAME infix_test_examples COMMAND infix_test dir ${CMAKE_CURRENT_SOURCE_DIR}/examples)

# fuzz all configs
add_test(
  NAME infix_test_fuzz_reader_base
  COMMAND infix_test fuzz
    reader)
add_test(
  NAME infix_test_fuzz_reader_tuples
  COMMAND infix_test fuzz
    --enable-tuples
    reader)
add_test(
  NAME infix_test_fuzz_reader_tuples_parens
  COMMAND infix_test fuzz
    --enable-tuples
    --tuples-require-parens
    reader)
add_test(
  NAME infix_test_fuzz_reader_tuples_parens_use_parser
  COMMAND infix_test fuzz
    --enable-tuples
    --tuples-require-parens
    --use-parser-tuples
    reader)
# note: calculate does not depend on configs so just one version is fine
add_test(
  NAME infix_test_fuzz_calculate
  COMMAND infix_test fuzz calculate)

add_executable(infix
  reader.cc
  writers.cc
  infix.cc
  parse.cc
)

target_link_libraries(infix
  trieste::trieste
)

install(TARGETS infix infix_trieste RUNTIME DESTINATION infix)
install(DIRECTORY examples DESTINATION infix)
