add_executable(lmsys main.cpp)
target_link_libraries(lmsys PRIVATE leon-ipc)
install(TARGETS lmsys RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
