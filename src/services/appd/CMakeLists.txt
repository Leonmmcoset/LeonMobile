add_executable(leon-launchd main.cpp server.cpp broker.cpp processes.cpp settings.cpp)
target_link_libraries(leon-launchd PRIVATE leon-package)
install(TARGETS leon-launchd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
