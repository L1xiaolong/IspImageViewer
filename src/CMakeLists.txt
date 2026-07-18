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

set(_ispview_runtime_icon "${PROJECT_SOURCE_DIR}/assets/brand/app_icon.png")
if(EXISTS "${_ispview_runtime_icon}")
    set_source_files_properties("${_ispview_runtime_icon}" PROPERTIES
        QT_RESOURCE_ALIAS "brand/app_icon.png")
    qt_add_resources(ISPImageViewer "ispview_brand_assets"
        PREFIX "/"
        FILES "${_ispview_runtime_icon}"
    )
    target_compile_definitions(ISPImageViewer PRIVATE ISPVIEW_HAS_BRAND_ICON=1)
endif()

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

set(_ispview_macos_icon "${PROJECT_SOURCE_DIR}/assets/icons/macos/ISPImageViewer.icns")
if(APPLE AND EXISTS "${_ispview_macos_icon}")
    set_source_files_properties("${_ispview_macos_icon}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(ISPImageViewer PRIVATE "${_ispview_macos_icon}")
    set_target_properties(ISPImageViewer PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "ISPImageViewer.icns")
endif()

set(_ispview_windows_icon "${PROJECT_SOURCE_DIR}/assets/icons/windows/ISPImageViewer.ico")
if(WIN32 AND EXISTS "${_ispview_windows_icon}")
    set(_ispview_windows_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/ISPImageViewerIcon.rc")
    file(GENERATE OUTPUT "${_ispview_windows_icon_rc}"
        CONTENT "IDI_ICON1 ICON \"${_ispview_windows_icon}\"\n")
    # Keep the resource compiler target free of Qt include directories. MinGW windres invokes
    # its preprocessor through cmd.exe and can split paths containing spaces even when CMake
    # quotes them (for example D:/Program Files/msys64).
    add_library(ISPImageViewer_windows_icon OBJECT "${_ispview_windows_icon_rc}")
    target_sources(ISPImageViewer PRIVATE
        $<TARGET_OBJECTS:ISPImageViewer_windows_icon>
    )
endif()

unset(_ispview_bundle_name)
unset(_ispview_bundle_identifier)
unset(_ispview_runtime_icon)
unset(_ispview_macos_icon)
unset(_ispview_windows_icon)
unset(_ispview_windows_icon_rc)

ispview_enable_warnings(ISPImageViewer)
