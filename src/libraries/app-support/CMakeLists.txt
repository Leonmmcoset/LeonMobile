add_library(leon-app-support STATIC src/application.cpp src/keyboard.cpp)
target_include_directories(leon-app-support PUBLIC include)
target_link_libraries(leon-app-support PUBLIC LMUI::LMUI leon-ipc)
