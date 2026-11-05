add_executable(documentd main.c service.c snapshot.c)
target_compile_definitions(documentd PRIVATE _GNU_SOURCE)
target_link_libraries(documentd PRIVATE leon-system)
install(TARGETS documentd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
