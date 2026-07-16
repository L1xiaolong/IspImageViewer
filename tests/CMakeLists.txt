qt_add_executable(ispview_core_tests
    core_tests.cpp
)
target_link_libraries(ispview_core_tests PRIVATE ispview_core Qt6::Test)
ispview_enable_warnings(ispview_core_tests)
add_test(NAME ispview_core_tests COMMAND ispview_core_tests)

qt_add_executable(ispview_io_tests
    io_tests.cpp
)
target_link_libraries(ispview_io_tests PRIVATE ispview_io Qt6::Test)
target_compile_definitions(ispview_io_tests PRIVATE
    ISPVIEW_HAS_LIBRAW=$<BOOL:${ISPVIEW_HAS_LIBRAW}>
    ISPVIEW_HAS_EXIV2=$<BOOL:${ISPVIEW_HAS_EXIV2}>
    ISPVIEW_HAS_LCMS2=$<BOOL:${ISPVIEW_HAS_LCMS2}>
)
if(ISPVIEW_HAS_EXIV2)
    target_link_libraries(ispview_io_tests PRIVATE Exiv2::exiv2lib)
endif()
ispview_enable_warnings(ispview_io_tests)
add_test(NAME ispview_io_tests COMMAND ispview_io_tests)

qt_add_executable(ispview_ui_tests
    ui_tests.cpp
)
target_link_libraries(ispview_ui_tests PRIVATE ispview_ui Qt6::Test)
target_compile_definitions(ispview_ui_tests PRIVATE
    ISPVIEW_HAS_LIBRAW=$<BOOL:${ISPVIEW_HAS_LIBRAW}>
)
ispview_enable_warnings(ispview_ui_tests)
add_test(NAME ispview_ui_tests COMMAND ispview_ui_tests)
set_tests_properties(ispview_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(ispview_render_tests
    render_tests.cpp
)
target_link_libraries(ispview_render_tests PRIVATE ispview_io ispview_render Qt6::Test)
ispview_enable_warnings(ispview_render_tests)
add_test(NAME ispview_render_tests COMMAND ispview_render_tests)
