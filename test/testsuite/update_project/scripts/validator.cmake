# Append a marker to generated output while proving validator variables are
# isolated from the executor process.
if(NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR "Validator requires OUTPUT_DIR.")
endif()

# These mutations must remain local to the validator process.
set(TESTSUITE_GOLDENS)
set(TESTSUITE_ARTIFACTS)
file(APPEND "${OUTPUT_DIR}/stdout.txt" "validated\n")
