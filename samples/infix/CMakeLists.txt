add_executable(infix_trieste
  reader.cc
  writers.cc
  infix_trieste.cc
  parse.cc
  )
enable_warnings(infix_trieste)

target_link_libraries(infix_trieste
  trieste::trieste
)

add_test(NAME infix COMMAND infix_trieste test -f)
add_test(NAME invalid_input COMMAND infix ./infix)
set_property(TEST invalid_input PROPERTY WILL_FAIL On)

add_executable(infix
  reader.cc
  writers.cc
  infix.cc
  parse.cc
)
enable_warnings(infix)

target_link_libraries(infix
  trieste::trieste
)

add_subdirectory(testsuite)

install(TARGETS infix infix_trieste RUNTIME DESTINATION infix)
install(DIRECTORY testsuite/examples DESTINATION infix)
