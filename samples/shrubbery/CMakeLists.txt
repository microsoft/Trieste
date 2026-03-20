add_executable(shrubbery
  reader.cc
  parse.cc
  shrubbery.cc
  )
enable_warnings(shrubbery)

target_link_libraries(shrubbery
  trieste::trieste
)

add_test(NAME shrubbery COMMAND shrubbery test -f)
add_test(NAME shrubbery_check COMMAND shrubbery check -w)
add_test(NAME shrubbery_sequence COMMAND shrubbery test -f --sequence)

install(TARGETS shrubbery RUNTIME DESTINATION shrubbery)
