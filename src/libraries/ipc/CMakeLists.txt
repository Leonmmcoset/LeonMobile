find_package(nlohmann_json 3.10 REQUIRED)
add_library(leon-ipc STATIC src/client.cpp)
target_include_directories(leon-ipc PUBLIC include)
target_link_libraries(leon-ipc PUBLIC nlohmann_json::nlohmann_json)
