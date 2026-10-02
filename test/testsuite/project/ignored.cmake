# This collection proves that explicit selection does not load every adjacent
# collection. It remains a valid no-op for scenarios exercising legacy
# implicit discovery.
if(SCENARIO STREQUAL "explicit-collections")
  message(FATAL_ERROR "Unselected collection was loaded.")
endif()

set(TESTSUITE_REGEX "^$")
set(TESTSUITE_DEFINE define_ignored)

function(define_ignored input)
endfunction()