find_package(nlohmann_json 3.10 REQUIRED)
set_target_properties(leon-ipc PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(leon-platform SHARED client.cpp)
add_library(LeonPlatform::Platform ALIAS leon-platform)
target_include_directories(leon-platform PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(leon-platform PUBLIC cxx_std_20)
target_link_libraries(leon-platform PRIVATE leon-ipc PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(leon-platform PROPERTIES VERSION 1.0.0 SOVERSION 1 EXPORT_NAME Platform)
install(TARGETS leon-platform EXPORT LeonPlatformTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
  NAMELINK_COMPONENT Development)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Development)
install(EXPORT LeonPlatformTargets NAMESPACE LeonPlatform:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeonPlatform COMPONENT Development)
include(CMakePackageConfigHelpers)
configure_package_config_file(LeonPlatformConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/LeonPlatformConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeonPlatform)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/LeonPlatformConfigVersion.cmake VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
configure_file(leon-platform.pc.in ${CMAKE_CURRENT_BINARY_DIR}/leon-platform.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/leon-platform.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig COMPONENT Development)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/LeonPlatformConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/LeonPlatformConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LeonPlatform COMPONENT Development)
if(BUILD_TESTING)
  add_executable(platform-sdk-test ../../../tests/platform_sdk.cpp)
  target_link_libraries(platform-sdk-test PRIVATE LeonPlatform::Platform)
endif()
