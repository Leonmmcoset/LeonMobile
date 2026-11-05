add_library(leon-log-client STATIC client.c capture.c)
target_compile_definitions(leon-log-client PRIVATE _GNU_SOURCE)
target_include_directories(leon-log-client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
