add_library(leon-root-wire STATIC wire.c exchange.c)
target_compile_definitions(leon-root-wire PRIVATE _GNU_SOURCE)
target_include_directories(leon-root-wire PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(leon-root-wire PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(LEON_SYSTEM OR LEON_TOOLS)
find_package(nlohmann_json 3.10 REQUIRED)
add_library(leon-root SHARED application.cpp)
add_library(LeonRoot::Root ALIAS leon-root)
target_compile_definitions(leon-root PRIVATE _GNU_SOURCE)
target_compile_features(leon-root PUBLIC cxx_std_20)
target_include_directories(leon-root PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(leon-root PRIVATE leon-root-wire nlohmann_json::nlohmann_json)
set_target_properties(leon-root PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 1 EXPORT_NAME Root)
include(CMakePackageConfigHelpers)
configure_package_config_file(LeonRootConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/LeonRootConfig.cmake
                              INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeonRoot)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/LeonRootConfigVersion.cmake
                                VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS leon-root EXPORT LeonRootTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime NAMELINK_COMPONENT Development)
install(DIRECTORY include/leon DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development)
install(EXPORT LeonRootTargets NAMESPACE LeonRoot:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeonRoot COMPONENT Development)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/LeonRootConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/LeonRootConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeonRoot COMPONENT Development)
configure_file(leon-root.pc.in leon-root.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/leon-root.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig COMPONENT Development)
if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(root-sdk-fixture ${PROJECT_SOURCE_DIR}/tests/root_sdk.cpp)
  target_link_libraries(root-sdk-fixture PRIVATE leon-root nlohmann_json::nlohmann_json Threads::Threads)
endif()

endif()
