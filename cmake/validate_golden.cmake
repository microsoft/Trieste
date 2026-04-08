# Validates test output before it is accepted into golden files.
#
# Required variables:
#   OUTPUT_DIR  — directory containing the test output (exit_code.txt, etc.)
#
# Optional variables:
#   VALIDATOR   — path to a user-supplied CMake script for additional checks.
#                 The script is invoked with OUTPUT_DIR set. It should call
#                 message(FATAL_ERROR ...) to reject the output.
#
# Built-in checks:
#   - exit_code.txt must exist and contain a plain integer. CMake writes
#     signal names (e.g. "Segmentation fault", "Aborted") for crashes, so
#     a non-integer value indicates a crash that must not become a golden file.

if(NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR "validate_golden.cmake: OUTPUT_DIR is required")
endif()

# --- Built-in crash detection -------------------------------------------

set(_exit_code_file "${OUTPUT_DIR}/exit_code.txt")

if(NOT EXISTS "${_exit_code_file}")
  message(FATAL_ERROR
    "validate_golden: ${_exit_code_file} does not exist. "
    "The test may not have run.")
endif()

file(READ "${_exit_code_file}" _exit_code)
string(STRIP "${_exit_code}" _exit_code)

# A valid exit code is an optional minus sign followed by one or more digits.
string(REGEX MATCH "^-?[0-9]+$" _match "${_exit_code}")
if(_match STREQUAL "")
  message(FATAL_ERROR
    "validate_golden: exit_code.txt contains '${_exit_code}', which is not a "
    "numeric exit code. This usually means the program crashed (e.g. "
    "Segmentation fault, Aborted). Refusing to update golden files.\n"
    "  Output directory: ${OUTPUT_DIR}")
endif()

# --- Optional user-supplied validator ------------------------------------

if(DEFINED VALIDATOR AND NOT VALIDATOR STREQUAL "")
  if(NOT EXISTS "${VALIDATOR}")
    message(FATAL_ERROR
      "validate_golden: VALIDATOR script '${VALIDATOR}' does not exist.")
  endif()
  include("${VALIDATOR}")
endif()
