find_package(JPEG REQUIRED)
leon_add_application(images SOURCES main.cpp view.cpp decoder.cpp)
target_link_libraries(leon-images PRIVATE JPEG::JPEG)
