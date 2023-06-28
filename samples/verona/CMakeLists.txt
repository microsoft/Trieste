find_package(Threads REQUIRED)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG 9.1.0
  GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(fmt)

set_target_properties(fmt PROPERTIES EXCLUDE_FROM_ALL ON)

add_executable(verona
  lang.cc
  lookup.cc
  main.cc
  parse.cc
  subtype.cc
  passes/modules.cc
  )

target_link_libraries(verona
  Threads::Threads
  fmt::fmt
  trieste::trieste
  )

add_test(NAME verona COMMAND verona test -f)

install(TARGETS verona RUNTIME DESTINATION verona)
install(DIRECTORY std DESTINATION verona)
