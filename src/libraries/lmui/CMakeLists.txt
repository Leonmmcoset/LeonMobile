pkg_check_modules(LMUI_CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(LMUI_PLATFORM REQUIRED IMPORTED_TARGET wayland-client pangocairo xkbcommon)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
add_custom_command(OUTPUT xdg-shell-client-protocol.h COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} xdg-shell-client-protocol.h DEPENDS ${XDG_SHELL_XML})
add_custom_command(OUTPUT xdg-shell-protocol.c COMMAND ${WAYLAND_SCANNER} private-code ${XDG_SHELL_XML} xdg-shell-protocol.c DEPENDS ${XDG_SHELL_XML})
add_library(lmui SHARED src/theme.cpp src/canvas.cpp src/widgets.cpp src/application.cpp
            src/motion.cpp src/touch.cpp src/transition.cpp
            src/wayland/connection.cpp src/wayland/input.cpp
            src/wayland/buffers.cpp src/wayland/render.cpp
            xdg-shell-protocol.c xdg-shell-client-protocol.h)
add_library(LMUI::LMUI ALIAS lmui)
target_compile_features(lmui PUBLIC cxx_std_20)
target_include_directories(lmui PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
                          PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(lmui PUBLIC PkgConfig::LMUI_CAIRO PRIVATE PkgConfig::LMUI_PLATFORM)
set_target_properties(lmui PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 1 EXPORT_NAME LMUI)
include(CMakePackageConfigHelpers)
configure_package_config_file(LMUIConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/LMUIConfig.cmake
                              INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LMUI)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/LMUIConfigVersion.cmake
                                VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS lmui EXPORT LMUITargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime NAMELINK_COMPONENT Development)
install(DIRECTORY include/lmui DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development)
install(EXPORT LMUITargets NAMESPACE LMUI:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LMUI COMPONENT Development)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/LMUIConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/LMUIConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LMUI COMPONENT Development)
configure_file(lmui.pc.in lmui.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lmui.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig COMPONENT Development)
if(BUILD_TESTING)
  add_executable(lmui-test ${PROJECT_SOURCE_DIR}/tests/lmui.cpp)
  target_link_libraries(lmui-test PRIVATE lmui)
  add_test(NAME lmui-layout-and-hit-test COMMAND lmui-test)
  add_executable(lmui-motion-test ${PROJECT_SOURCE_DIR}/tests/motion.cpp)
  target_link_libraries(lmui-motion-test PRIVATE lmui)
  add_test(NAME lmui-motion-and-touch COMMAND lmui-motion-test)
  add_executable(lmui-wayland-motion-test ${PROJECT_SOURCE_DIR}/tests/wayland_motion.cpp)
  target_link_libraries(lmui-wayland-motion-test PRIVATE lmui)
endif()
