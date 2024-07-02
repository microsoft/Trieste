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

add_executable(infix_progspace_test
  progspace_test.cc
)

target_link_libraries(infix_progspace_test
  trieste::trieste
)
 
add_test(NAME infix COMMAND infix_trieste test -f)
add_test(NAME invalid_input COMMAND infix ./infix)
set_property(TEST invalid_input PROPERTY WILL_FAIL On)
add_test(NAME infix_test_examples COMMAND infix_test dir ${CMAKE_CURRENT_SOURCE_DIR}/examples)

# these params are the "useful" ones, in that they will test nested operator parsing, associativity, etc
# they are also rather CPU hungry (~5min on my machine), so maybe this should go under a flag or something
add_test(NAME infix_test_bfs COMMAND infix_test bfs_test --op-count 1 --depth 2)

add_test(NAME infix_progspace_test COMMAND infix_progspace_test)

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

add_executable(bfs_test
  bfs_test.cc
)

target_link_libraries(infix
  trieste::trieste
)

install(TARGETS infix infix_trieste RUNTIME DESTINATION infix)
install(DIRECTORY examples DESTINATION infix)
