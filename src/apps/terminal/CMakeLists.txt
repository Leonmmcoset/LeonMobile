pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
leon_add_application(terminal SOURCES main.cpp view.cpp terminal.cpp)
target_link_libraries(leon-terminal PRIVATE PkgConfig::VTERM util)
