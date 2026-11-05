find_package(Threads REQUIRED)
leon_add_application(settings SOURCES main.cpp application.cpp catalog.cpp device.cpp
                     view.cpp preferences.cpp platform.cpp date_time.cpp notification_settings.cpp system_preferences.cpp device_view.cpp widgets.cpp developer.cpp security.cpp
                     applications.cpp applications_view.cpp root_apps.cpp diagnostics.cpp diagnostics_view.cpp)
target_link_libraries(leon-settings PRIVATE leon-root-wire leon-auth-client leon-log-client Threads::Threads)
