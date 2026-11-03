pkg_check_modules(WLR REQUIRED IMPORTED_TARGET wlroots-0.20 wayland-server xkbcommon)
add_executable(leon-compositor main.c input.c keyboard.c pointer.c touch.c
                output.c windows.c focus.c control.c)
target_compile_definitions(leon-compositor PRIVATE WLR_USE_UNSTABLE _GNU_SOURCE)
target_compile_options(leon-compositor PRIVATE -Wno-unused-parameter)
target_link_libraries(leon-compositor PRIVATE PkgConfig::WLR)
install(TARGETS leon-compositor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
