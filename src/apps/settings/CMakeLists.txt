leon_add_application(settings SOURCES main.cpp application.cpp catalog.cpp device.cpp
                     view.cpp preferences.cpp device_view.cpp widgets.cpp developer.cpp)
target_link_libraries(leon-settings PRIVATE leon-root-wire)
