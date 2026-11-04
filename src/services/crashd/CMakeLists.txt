add_executable(crashd main.c service.c)
target_compile_definitions(crashd PRIVATE _GNU_SOURCE)
target_link_libraries(crashd PRIVATE leon-system)
install(TARGETS crashd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
