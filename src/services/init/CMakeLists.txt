add_executable(leon-init main.c boot.c early.c health.c shutdown.c)
target_link_libraries(leon-init PRIVATE leon-boot-log leon-boot-state)
target_compile_definitions(leon-init PRIVATE _GNU_SOURCE)
install(TARGETS leon-init RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
