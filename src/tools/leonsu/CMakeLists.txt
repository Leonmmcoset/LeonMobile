add_executable(leonsu main.c)
target_compile_definitions(leonsu PRIVATE _GNU_SOURCE)
target_link_libraries(leonsu PRIVATE leon-root-wire leon-system)
install(TARGETS leonsu RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
