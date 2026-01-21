# Helper script to copy a file only if it exists and differs from the target.
# Usage: cmake -Dsrc=<source> -Ddst=<destination> -P copy_if_different_and_exists.cmake

if(NOT DEFINED src)
  message(FATAL_ERROR "copy_if_different_and_exists.cmake requires src")
endif()
if(NOT DEFINED dst)
  message(FATAL_ERROR "copy_if_different_and_exists.cmake requires dst")
endif()

# Resolve sources. Support globs (e.g. dir/*) by expanding; fall back to the
# literal path if no glob match but the file exists.
file(GLOB _resolved "${src}")
if(_resolved STREQUAL "")
  if(EXISTS "${src}")
    list(APPEND _resolved "${src}")
  endif()
endif()

if(_resolved STREQUAL "")
  # Nothing to copy; warn so callers know the source was missing.
  message(WARNING "copy_if_different_and_exists: no source found for '${src}'")
  return()
endif()

foreach(_s IN LISTS _resolved)
  if(EXISTS "${_s}")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_s}" "${dst}"
      RESULT_VARIABLE _copy_res)
    if(NOT _copy_res EQUAL 0)
      message(FATAL_ERROR "copy_if_different failed for '${_s}' -> '${dst}'")
    endif()
  endif()
endforeach()
