add_executable(leon-shell main.cpp)
target_link_libraries(leon-shell PRIVATE leon-app-support)
install(TARGETS leon-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
