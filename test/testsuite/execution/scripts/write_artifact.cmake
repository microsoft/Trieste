# Produce the transient payload consumed by the dependent execution node.
if(NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "OUTPUT_FILE is required.")
endif()

file(WRITE "${OUTPUT_FILE}" "payload\n")
