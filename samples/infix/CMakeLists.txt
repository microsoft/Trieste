add_executable(infix
  lang.cc
  main.cc
  parse.cc
  )

target_link_libraries(infix
  snmallocshim-static
  re2::re2
  CLI11::CLI11
  trieste::trieste
  )

add_test(NAME infix COMMAND infix test -f)

install(TARGETS infix RUNTIME DESTINATION infix)
install(DIRECTORY examples DESTINATION infix)
