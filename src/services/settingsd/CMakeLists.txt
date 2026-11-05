add_executable(settingsd main.c schema.c store.c service.c)
target_compile_definitions(settingsd PRIVATE _GNU_SOURCE)
target_link_libraries(settingsd PRIVATE leon-system)
install(TARGETS settingsd RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR} COMPONENT Runtime)
