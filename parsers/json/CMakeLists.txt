set( SOURCES
parse.cc
reader.cc
writer.cc
)

if( BUILD_SHARED_LIBS )
  add_library(json SHARED ${SOURCES})
else()
  add_library(json STATIC ${SOURCES})
endif()

add_library(trieste::json ALIAS json)

target_link_libraries(json
  PUBLIC
    trieste::trieste
)

if(MSVC)
  target_compile_options(json PUBLIC "/Zc:__cplusplus")
  target_compile_definitions(json PUBLIC "_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING")
endif()

if(TRIESTE_USE_CXX17)
  target_compile_features(json PUBLIC cxx_std_17)
  target_compile_definitions(json PUBLIC TRIESTE_USE_CXX17)
else()
  target_compile_features(json PUBLIC cxx_std_20)
endif()

target_include_directories( json
  PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include/trieste>
)

install(TARGETS json
  EXPORT ${PROJECT_NAME}_Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../include/trieste/json.h DESTINATION include/trieste)
