find_package(Threads REQUIRED)

add_executable(verona
  lang.cc
  lookup.cc
  main.cc
  parse.cc
  )

target_link_libraries(verona
  Threads::Threads
  CLI11::CLI11
  trieste::trieste
  )

enable_testing()

add_test(NAME verona COMMAND verona test)

install(TARGETS verona RUNTIME DESTINATION verona)
install(DIRECTORY std DESTINATION verona)
