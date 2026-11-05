add_executable(intentd main.c policy.c service.c delivery.c)
target_compile_definitions(intentd PRIVATE _GNU_SOURCE)
target_link_libraries(intentd PRIVATE leon-system)
install(TARGETS intentd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
