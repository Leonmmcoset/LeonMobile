add_executable(leon-shell main.cpp session.cpp navigation.cpp render.cpp control.cpp
                views/status.cpp views/lock.cpp views/home.cpp views/tasks.cpp
                views/quick.cpp views/notifications.cpp views/navigation.cpp)
target_link_libraries(leon-shell PRIVATE leon-app-support)
install(TARGETS leon-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
