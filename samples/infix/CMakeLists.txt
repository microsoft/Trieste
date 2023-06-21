add_executable(infix
  lang.cc
  main.cc
  parse.cc
  )

target_link_libraries(infix
  trieste::trieste
  )

add_test(NAME infix COMMAND infix test -f)

install(TARGETS infix RUNTIME DESTINATION infix)
install(DIRECTORY examples DESTINATION infix)
