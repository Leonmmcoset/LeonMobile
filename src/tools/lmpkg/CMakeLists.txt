add_executable(lmpkg main.cpp)
target_link_libraries(lmpkg PRIVATE leon-package leon-ipc)
install(TARGETS lmpkg RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
