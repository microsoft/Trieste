# Configure the disposable fixture used to test golden update behavior.
cmake_minimum_required(VERSION 3.28)
project(testsuite_update_graph LANGUAGES CXX)

include("${TESTSUITE_CMAKE}")
add_executable(update_producer EXCLUDE_FROM_ALL scripts/produce.cc)
target_compile_features(update_producer PRIVATE cxx_std_17)
testsuite(update-graph)
