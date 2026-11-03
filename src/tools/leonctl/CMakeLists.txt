add_executable(leonctl main.c)
target_compile_definitions(leonctl PRIVATE _GNU_SOURCE)
install(TARGETS leonctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
