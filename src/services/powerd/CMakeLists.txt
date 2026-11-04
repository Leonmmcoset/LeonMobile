add_executable(powerd main.c service.c hardware.c)
target_compile_definitions(powerd PRIVATE _GNU_SOURCE)
target_link_libraries(powerd PRIVATE leon-system leon-auth-client)
install(TARGETS powerd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
