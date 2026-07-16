qt_add_library(ispview_ui STATIC
    histogram_panel.h
    histogram_panel.cpp
    image_info_panel.h
    image_info_panel.cpp
    image_properties_panel.h
    image_properties_panel.cpp
    thumbnail_model.h
    thumbnail_model.cpp
    thumbnail_view.h
    thumbnail_view.cpp
    thumbnail_item_delegate.h
    thumbnail_item_delegate.cpp
    local_file_drop.h
    local_file_drop.cpp
    file_clipboard.h
    file_clipboard.cpp
    path_breadcrumb.h
    path_breadcrumb.cpp
    thumbnail_filter_proxy_model.h
    thumbnail_filter_proxy_model.cpp
    full_screen_window.h
    full_screen_window.cpp
    compare_window.h
    compare_window.cpp
    main_window.h
    main_window.cpp
    multi_folder_window.h
    multi_folder_window.cpp
    trash_confirmation.h
    trash_confirmation.cpp
    raw_parameter_panel.h
    raw_parameter_panel.cpp
)

target_include_directories(ispview_ui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ispview_ui
    PUBLIC ispview_core ispview_io ispview_render ispview_platform Qt6::Widgets
)
ispview_enable_warnings(ispview_ui)
