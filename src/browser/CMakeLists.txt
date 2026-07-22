qt_add_library(ispview_browser_backend STATIC
    thumbnail_model.h
    thumbnail_model.cpp
    local_file_drop.h
    local_file_drop.cpp
    file_clipboard.h
    file_clipboard.cpp
    thumbnail_filter_proxy_model.h
    thumbnail_filter_proxy_model.cpp
)

target_include_directories(ispview_browser_backend PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ispview_browser_backend
    PUBLIC ispview_core ispview_io ispview_platform Qt6::Gui
)
ispview_enable_warnings(ispview_browser_backend)
