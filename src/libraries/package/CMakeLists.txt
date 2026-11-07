find_package(OpenSSL REQUIRED)
find_package(LibArchive REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
add_library(leon-package STATIC src/manifest.cpp src/json.cpp src/catalog.cpp
                               src/archive.cpp src/installer.cpp src/inspection.cpp src/root.cpp)
target_include_directories(leon-package PUBLIC include)
target_link_libraries(leon-package PUBLIC nlohmann_json::nlohmann_json
                      PRIVATE OpenSSL::Crypto LibArchive::LibArchive)
