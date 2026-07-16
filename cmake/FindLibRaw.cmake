# Locate LibRaw without relying on a package-provided CMake config. Some LibRaw
# installations ship configs containing machine-specific paths, while pkg-config
# and the conventional include/library layout remain relocatable.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LibRaw QUIET libraw)
endif()

find_path(LibRaw_INCLUDE_DIR
    NAMES libraw/libraw.h
    HINTS ${PC_LibRaw_INCLUDE_DIRS}
)

# Prefer the reentrant variant when a distribution provides both libraries.
find_library(LibRaw_REENTRANT_LIBRARY
    NAMES raw_r libraw_r
    HINTS ${PC_LibRaw_LIBRARY_DIRS}
)
find_library(LibRaw_STANDARD_LIBRARY
    NAMES raw libraw
    HINTS ${PC_LibRaw_LIBRARY_DIRS}
)

if(LibRaw_REENTRANT_LIBRARY)
    set(LibRaw_LIBRARY "${LibRaw_REENTRANT_LIBRARY}")
    set(LibRaw_IS_REENTRANT TRUE)
else()
    set(LibRaw_LIBRARY "${LibRaw_STANDARD_LIBRARY}")
    set(LibRaw_IS_REENTRANT FALSE)
endif()
get_filename_component(LibRaw_LIBRARY_DIR "${LibRaw_LIBRARY}" DIRECTORY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibRaw
    REQUIRED_VARS LibRaw_INCLUDE_DIR LibRaw_LIBRARY
    VERSION_VAR PC_LibRaw_VERSION
)

if(LibRaw_FOUND AND NOT TARGET LibRaw::LibRaw)
    add_library(LibRaw::LibRaw UNKNOWN IMPORTED)
    set_target_properties(LibRaw::LibRaw PROPERTIES
        IMPORTED_LOCATION "${LibRaw_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibRaw_INCLUDE_DIR};${LibRaw_INCLUDE_DIR}/libraw"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
            "${LibRaw_INCLUDE_DIR};${LibRaw_INCLUDE_DIR}/libraw"
    )
endif()

mark_as_advanced(
    LibRaw_INCLUDE_DIR
    LibRaw_LIBRARY
    LibRaw_LIBRARY_DIR
    LibRaw_REENTRANT_LIBRARY
    LibRaw_STANDARD_LIBRARY
)
