# Each built-in owns its source list and manifest; there is no mode dispatcher.
function(leon_add_application name)
  cmake_parse_arguments(APP "" "" "SOURCES" ${ARGN})
  if(NOT APP_SOURCES)
    message(FATAL_ERROR "Application ${name} needs an explicit source list")
  endif()
  add_executable(leon-${name} ${APP_SOURCES})
  target_link_libraries(leon-${name} PRIVATE leon-app-support)
  install(TARGETS leon-${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
  install(FILES manifest.json DESTINATION ${CMAKE_INSTALL_DATADIR}/leon/apps
          RENAME ${name}.json COMPONENT Runtime)
endfunction()
