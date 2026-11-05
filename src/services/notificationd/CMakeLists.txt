add_executable(notificationd main.c server.c store.c protocol.cpp policy.cpp)
target_compile_definitions(notificationd PRIVATE _GNU_SOURCE)
target_link_libraries(notificationd PRIVATE leon-package leon-system leon-auth-client)
install(TARGETS notificationd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
