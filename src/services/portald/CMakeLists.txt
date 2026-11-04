add_executable(portald main.c service.c files.c)
target_compile_definitions(portald PRIVATE _GNU_SOURCE)
target_link_libraries(portald PRIVATE leon-system)
install(TARGETS portald RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
