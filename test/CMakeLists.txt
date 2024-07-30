
add_executable(trieste_source_test
  source_test.cc
)
target_link_libraries(trieste_source_test trieste::trieste)

add_test(NAME trieste_source_test COMMAND trieste_source_test --depth 6)
