set( SOURCES
parse.cc
event_writer.cc
writer.cc
reader.cc
to_json.cc
)

if( BUILD_SHARED_LIBS )
  add_library(yaml SHARED ${SOURCES})
else()
  add_library(yaml STATIC ${SOURCES})
endif()
enable_warnings(yaml)

add_library(trieste::yaml ALIAS yaml)

target_link_libraries(yaml
  PUBLIC
    trieste::trieste
    trieste::json
)

if(MSVC)
  target_compile_options(yaml PUBLIC "/Zc:__cplusplus")
  target_compile_definitions(yaml PUBLIC "_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING")
endif()

if(YAML_USE_CXX17)
  target_compile_features(yaml PUBLIC cxx_std_17)
  target_compile_definitions(yaml PUBLIC YAML_USE_CXX17)
else()
  target_compile_features(yaml PUBLIC cxx_std_20)
endif()

target_include_directories( yaml
  PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include/trieste>
)

install(TARGETS yaml
  EXPORT ${PROJECT_NAME}_Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../include/trieste/yaml.h DESTINATION include/trieste)

if( TRIESTE_BUILD_PARSER_TOOLS )
  add_executable(yamlc yamlc.cc)
  enable_warnings(yamlc)

  target_link_libraries(yamlc
    PRIVATE
      yaml
  )

  install(TARGETS yamlc RUNTIME DESTINATION parsers)
endif()
