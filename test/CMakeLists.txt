
add_executable(trieste_intrusive_ptr_test
  intrusive_ptr_test.cc
)
enable_warnings(trieste_intrusive_ptr_test)
target_link_libraries(trieste_intrusive_ptr_test trieste::trieste)

# This test might not make so much sense without asan enabled, but might as well
# check that the test compiles and doesn't crash on other compilers.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT TRIESTE_SANITIZE)
  target_compile_options(trieste_intrusive_ptr_test PUBLIC -g -fsanitize=thread)
  target_link_libraries(trieste_intrusive_ptr_test -fsanitize=thread)
endif()

add_test(NAME trieste_intrusive_ptr_test COMMAND trieste_intrusive_ptr_test WORKING_DIRECTORY $<TARGET_FILE_DIR:trieste_intrusive_ptr_test>)

add_executable(trieste_source_test
  source_test.cc
)
enable_warnings(trieste_source_test)
target_link_libraries(trieste_source_test trieste::trieste)

add_test(NAME trieste_source_test COMMAND trieste_source_test --depth 6 WORKING_DIRECTORY $<TARGET_FILE_DIR:trieste_source_test>)

add_executable(trieste_nodeworker_test
  nodeworker_test.cc
)
enable_warnings(trieste_nodeworker_test)
target_link_libraries(trieste_nodeworker_test trieste::trieste)

add_test(NAME trieste_nodeworker_test COMMAND trieste_nodeworker_test WORKING_DIRECTORY $<TARGET_FILE_DIR:trieste_nodeworker_test>)

add_executable(trieste_regex_engine_test
  regex_engine_test.cc
)
enable_warnings(trieste_regex_engine_test)
target_link_libraries(trieste_regex_engine_test trieste::trieste)

add_test(NAME trieste_regex_engine_test COMMAND trieste_regex_engine_test WORKING_DIRECTORY $<TARGET_FILE_DIR:trieste_regex_engine_test>)

if(TRIESTE_BUILD_REGEX_BENCHMARK)
  include(FetchContent)

  FetchContent_Declare(
    re2
    GIT_REPOSITORY https://github.com/google/re2
    GIT_TAG 4be240789d5b322df9f02b7e19c8651f3ccbf205
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
  )
  set(RE2_BUILD_TESTING OFF CACHE BOOL "Disable RE2 tests for Trieste benchmark builds" FORCE)
  FetchContent_MakeAvailable(re2)

  if(NOT TARGET re2::re2)
    message(FATAL_ERROR "TRIESTE_BUILD_REGEX_BENCHMARK=ON requires RE2 target re2::re2. Configure with network access, or provide a local RE2 source via FetchContent overrides.")
  endif()

  add_executable(trieste_regex_engine_benchmark
    regex_engine_benchmark.cc
  )
  enable_warnings(trieste_regex_engine_benchmark)
  target_compile_definitions(trieste_regex_engine_benchmark
    PRIVATE
      TRIESTE_REGEX_ENGINE_ENABLE_STATS=1
  )
  target_link_libraries(trieste_regex_engine_benchmark
    PRIVATE
      trieste::trieste
      re2::re2
  )

  add_executable(trieste_tregex_benchmark
    tregex_benchmark.cc
  )
  enable_warnings(trieste_tregex_benchmark)
  target_link_libraries(trieste_tregex_benchmark
    PRIVATE
      trieste::trieste
  )
endif()
