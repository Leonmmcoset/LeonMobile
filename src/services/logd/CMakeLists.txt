add_executable(logd main.c record.c storage.c query.c)
target_compile_definitions(logd PRIVATE _GNU_SOURCE)
target_link_libraries(logd PRIVATE leon-system leon-log-client leon-auth-client)
install(TARGETS logd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
