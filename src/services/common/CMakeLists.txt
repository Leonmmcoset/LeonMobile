add_library(leon-system STATIC server.c runtime.c json.cpp)
target_compile_definitions(leon-system PRIVATE _GNU_SOURCE)
target_include_directories(leon-system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(leon-system PRIVATE leon-package)
