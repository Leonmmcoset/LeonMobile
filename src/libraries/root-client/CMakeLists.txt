add_library(leon-root-wire STATIC wire.c)
target_compile_definitions(leon-root-wire PRIVATE _GNU_SOURCE)
target_include_directories(leon-root-wire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
