add_executable(leon-shell main.cpp session.cpp system.cpp navigation.cpp render.cpp control.cpp shade.cpp
                views/status.cpp views/lock.cpp views/home.cpp views/tasks.cpp
                views/shade.cpp views/shade_widgets.cpp views/quick.cpp
                views/notifications.cpp views/navigation.cpp)
target_link_libraries(leon-shell PRIVATE leon-app-support)
install(TARGETS leon-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
if(BUILD_TESTING)
  add_executable(shell-shade-test ${PROJECT_SOURCE_DIR}/tests/shell_shade.cpp shade.cpp)
  target_include_directories(shell-shade-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(shell-shade-test PRIVATE LMUI::LMUI)
  add_test(NAME shell-shade-gestures COMMAND shell-shade-test)
endif()
