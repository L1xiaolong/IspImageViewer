qt_add_executable(ispview_raw_benchmark
    raw_decode_benchmark.cpp
)

target_link_libraries(ispview_raw_benchmark PRIVATE ispview_io Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_raw_benchmark)

qt_add_executable(ispview_histogram_benchmark
    histogram_benchmark.cpp
)

target_link_libraries(ispview_histogram_benchmark PRIVATE ispview_core Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_histogram_benchmark)

qt_add_executable(ispview_color_benchmark
    color_management_benchmark.cpp
)

target_link_libraries(ispview_color_benchmark PRIVATE ispview_io Qt6::Core Qt6::Gui)
ispview_enable_warnings(ispview_color_benchmark)

qt_add_executable(ispview_gpu_benchmark
    gpu_render_benchmark.cpp
)

target_link_libraries(ispview_gpu_benchmark PRIVATE
    ispview_io
    ispview_render
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
ispview_enable_warnings(ispview_gpu_benchmark)

qt_add_executable(ispview_ui_benchmark
    ui_navigation_benchmark.cpp
)

target_link_libraries(ispview_ui_benchmark PRIVATE
    ispview_ui
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    Qt6::Widgets
)
ispview_enable_warnings(ispview_ui_benchmark)

qt_add_executable(ispview_sample_check
    sample_decode_check.cpp
)

target_link_libraries(ispview_sample_check PRIVATE
    ispview_io
    Qt6::Core
    Qt6::Gui
)
ispview_enable_warnings(ispview_sample_check)
