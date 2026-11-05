add_executable(leon-launchd main.cpp server.cpp broker.cpp processes.cpp settings.cpp sandbox.c
                           management.cpp storage.cpp root.cpp)
target_link_libraries(leon-launchd PRIVATE leon-package leon-ipc leon-system leon-auth-client)
install(TARGETS leon-launchd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
