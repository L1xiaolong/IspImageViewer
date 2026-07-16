qt_add_library(ispview_platform STATIC
    platform_services.h
    platform_services.cpp
    platform_shortcuts.h
    platform_shortcuts.cpp
)

target_include_directories(ispview_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ispview_platform PUBLIC Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_platform)
