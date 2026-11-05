add_executable(leonlog main.cpp)
target_link_libraries(leonlog PRIVATE leon-log-client leon-ipc)
install(TARGETS leonlog RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
