add_executable(netd main.c service.c)
target_compile_definitions(netd PRIVATE _GNU_SOURCE)
target_link_libraries(netd PRIVATE leon-system)
install(TARGETS netd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
