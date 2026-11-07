find_package(Threads REQUIRED)
leon_add_application(installer SOURCES main.cpp application.cpp view.cpp)
target_link_libraries(leon-installer PRIVATE Threads::Threads)
