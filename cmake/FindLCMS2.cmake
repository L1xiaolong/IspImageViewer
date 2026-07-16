find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LCMS2 QUIET lcms2)
endif()

find_path(LCMS2_INCLUDE_DIR
    NAMES lcms2.h
    HINTS ${PC_LCMS2_INCLUDE_DIRS}
)
find_library(LCMS2_LIBRARY
    NAMES lcms2
    HINTS ${PC_LCMS2_LIBRARY_DIRS}
)

set(LCMS2_VERSION "${PC_LCMS2_VERSION}")
if(NOT LCMS2_VERSION AND LCMS2_INCLUDE_DIR)
    file(STRINGS "${LCMS2_INCLUDE_DIR}/lcms2.h" _lcms2_version_line
        REGEX "^#define[ \t]+LCMS_VERSION[ \t]+[0-9]+")
    if(_lcms2_version_line MATCHES "LCMS_VERSION[ \t]+([0-9]+)")
        set(_lcms2_version_number "${CMAKE_MATCH_1}")
        math(EXPR _lcms2_version_major "${_lcms2_version_number} / 1000")
        math(EXPR _lcms2_version_minor "(${_lcms2_version_number} % 1000) / 10")
        set(LCMS2_VERSION "${_lcms2_version_major}.${_lcms2_version_minor}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LCMS2
    REQUIRED_VARS LCMS2_LIBRARY LCMS2_INCLUDE_DIR
    VERSION_VAR LCMS2_VERSION
)

if(LCMS2_FOUND AND NOT TARGET LCMS2::LCMS2)
    add_library(LCMS2::LCMS2 UNKNOWN IMPORTED)
    set_target_properties(LCMS2::LCMS2 PROPERTIES
        IMPORTED_LOCATION "${LCMS2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LCMS2_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(LCMS2_INCLUDE_DIR LCMS2_LIBRARY)
