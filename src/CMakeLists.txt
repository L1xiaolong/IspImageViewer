add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(render)
add_subdirectory(platform)
add_subdirectory(ui)

qt_add_executable(ISPImageViewer
    app/main.cpp
)

# The executable is the composition root: it creates the production decoder from IO and injects
# that dependency into the UI. Keep both direct dependencies explicit even though UI also uses IO.
target_link_libraries(ISPImageViewer PRIVATE ispview_ui ispview_io)

set(_ispview_bundle_name "ISP Image Viewer")
set(_ispview_bundle_identifier "com.ispview.viewer")
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Keep development bundles distinguishable from Release and from old compatibility builds.
    # This lets LaunchServices, Accessibility and UI automation bind the exact executable under
    # test while QSettings still uses the stable application/organization names from main.cpp.
    set(_ispview_bundle_name "ISP Image Viewer Debug")
    set(_ispview_bundle_identifier "com.ispview.viewer.debug")
    set_target_properties(ISPImageViewer PROPERTIES OUTPUT_NAME "ISPImageViewerDebug")
endif()
set_target_properties(ISPImageViewer PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "${_ispview_bundle_name}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${_ispview_bundle_identifier}"
)
unset(_ispview_bundle_name)
unset(_ispview_bundle_identifier)

ispview_enable_warnings(ISPImageViewer)
