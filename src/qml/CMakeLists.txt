set_source_files_properties(Isp/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_policy(SET QTP0004 NEW)

qt_add_executable(ISPImageViewerQml
    main.cpp
    browse_controller.h
    browse_controller.cpp
    browse_workspace_controller.h
    browse_workspace_controller.cpp
    thumbnail_image_provider.h
    thumbnail_image_provider.cpp
    qml_image_canvas.h
    qml_image_canvas.cpp
    compare_controller.h
    compare_controller.cpp
    image_properties_controller.h
    image_properties_controller.cpp
    full_screen_controller.h
    full_screen_controller.cpp
    raw_parameters_controller.h
    raw_parameters_controller.cpp
)

qt_add_qml_module(ISPImageViewerQml
    URI ISPViewQml
    VERSION 1.0
    QML_FILES
        Main.qml
        Isp/Theme.qml
        Isp/AppIconButton.qml
        Isp/AppTextInputDialog.qml
        Isp/AppConfirmDialog.qml
        Isp/AppMenu.qml
        Isp/AppMenuItem.qml
        Isp/AppMenuSeparator.qml
        Isp/BrowserPane.qml
        Isp/CompareLumaHistogram.qml
        Isp/DesignCompareCanvas.qml
        Isp/DesignFullScreenCanvas.qml
        Isp/FolderNavigator.qml
        Isp/FullScreenPropertiesCard.qml
        Isp/ImagePropertiesDialog.qml
        Isp/PropertiesHistogram.qml
        Isp/PropertyFieldList.qml
        Isp/RawNumberField.qml
        Isp/RawParameterComboBox.qml
        Isp/RawParametersDialog.qml
        Isp/ProductionCompareCanvas.qml
        Isp/ProductionFullScreenCanvas.qml
        Isp/TopToolbar.ui.qml
        Isp/ThumbnailTile.qml
        Pages/BrowsePage.qml
        Pages/ComparePage.qml
        Pages/FullScreenPage.qml
)

set(_ispview_qml_icons
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/back.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/forward.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/up.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/folder.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/folder-open.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/folder-plus.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/folder-pane-plus.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/close.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/search.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/sort.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/copy.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/cut.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/paste.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/compare.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/multi-source.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/grid.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/list.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/gallery.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/columns.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/chevron-down.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/plus.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/info.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/sliders.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/star.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/trash.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/more.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/actual-size.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/side-by-side.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/split-vertical.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/fit.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/exif.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/histogram.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/pixel-probe.svg
    ${PROJECT_SOURCE_DIR}/assets/icons/ui/screenshot.svg
)
qt_add_resources(ISPImageViewerQml "ispview_qml_icons"
    PREFIX "/icons/ui"
    BASE "${PROJECT_SOURCE_DIR}/assets/icons/ui"
    FILES ${_ispview_qml_icons}
)

target_include_directories(ISPImageViewerQml PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ISPImageViewerQml PRIVATE
    ispview_browser_backend
    ispview_render_backend
    ispview_io
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickLayouts
    Qt6::QuickTemplates2
    Qt6::Svg
)
ispview_enable_warnings(ISPImageViewerQml)

# The QML executable is the sole production application artifact.
set_target_properties(ISPImageViewerQml PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    OUTPUT_NAME "ISPImageViewer"
    MACOSX_BUNDLE_BUNDLE_NAME "ISP Image Viewer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.ispview.viewer"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PROJECT_SOURCE_DIR}/dist"
)

set(_ispview_qml_macos_icon "${PROJECT_SOURCE_DIR}/assets/icons/macos/ISPImageViewer.icns")
if(APPLE AND EXISTS "${_ispview_qml_macos_icon}")
    set_source_files_properties("${_ispview_qml_macos_icon}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(ISPImageViewerQml PRIVATE "${_ispview_qml_macos_icon}")
    set_target_properties(ISPImageViewerQml PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "ISPImageViewer.icns")
endif()

# The QML application is the Windows production executable as well, so embed the
# same application icon used by the macOS bundle.
set(_ispview_qml_windows_icon "${PROJECT_SOURCE_DIR}/assets/icons/windows/ISPImageViewer.ico")
if(WIN32 AND EXISTS "${_ispview_qml_windows_icon}")
    set(_ispview_qml_windows_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/ISPImageViewerQmlIcon.rc")
    file(GENERATE OUTPUT "${_ispview_qml_windows_icon_rc}"
        CONTENT "IDI_ICON1 ICON \"${_ispview_qml_windows_icon}\"\n")
    add_library(ISPImageViewerQml_windows_icon OBJECT "${_ispview_qml_windows_icon_rc}")
    target_sources(ISPImageViewerQml PRIVATE
        $<TARGET_OBJECTS:ISPImageViewerQml_windows_icon>
    )
endif()

unset(_ispview_qml_icons)
unset(_ispview_qml_macos_icon)
unset(_ispview_qml_windows_icon)
unset(_ispview_qml_windows_icon_rc)
