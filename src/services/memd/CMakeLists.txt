add_executable(memd main.c service.c)
target_compile_definitions(memd PRIVATE _GNU_SOURCE)
target_link_libraries(memd PRIVATE leon-system)
install(TARGETS memd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
