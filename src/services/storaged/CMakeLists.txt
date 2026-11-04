add_executable(storaged main.c service.c)
target_compile_definitions(storaged PRIVATE _GNU_SOURCE)
target_link_libraries(storaged PRIVATE leon-system)
install(TARGETS storaged RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
