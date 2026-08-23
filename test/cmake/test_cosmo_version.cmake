cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED COSMO_SOURCE_DIR)
    message(FATAL_ERROR "COSMO_SOURCE_DIR is required")
endif()

include("${COSMO_SOURCE_DIR}/cmake/CosmoVersion.cmake")
cosmo_resolve_version("${COSMO_SOURCE_DIR}" 1 1 0 0)

foreach(_part MAJOR MINOR PATCH BUILD)
    if(NOT COSMO_VERSION_${_part} MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "COSMO_VERSION_${_part} is not numeric: ${COSMO_VERSION_${_part}}")
    endif()
endforeach()

set(_package_version
    "V${COSMO_VERSION_MAJOR}.${COSMO_VERSION_MINOR}.${COSMO_VERSION_PATCH}")
set(_display_version "${_package_version}.${COSMO_VERSION_BUILD}")

if(NOT _package_version MATCHES "^V[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Invalid package version: ${_package_version}")
endif()
if(NOT _display_version MATCHES "^V[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Invalid display version: ${_display_version}")
endif()

message(STATUS "Package version: ${_package_version}")
message(STATUS "Display version: ${_display_version}")
message(STATUS "Version source: ${COSMO_VERSION_SOURCE}")

# A source tree without Git metadata must retain the supplied release fallback.
cosmo_resolve_version("${COSMO_SOURCE_DIR}/path-without-git-metadata" 7 8 9 10)
if(NOT COSMO_VERSION_MAJOR EQUAL 7 OR
   NOT COSMO_VERSION_MINOR EQUAL 8 OR
   NOT COSMO_VERSION_PATCH EQUAL 9 OR
   NOT COSMO_VERSION_BUILD EQUAL 10 OR
   NOT COSMO_VERSION_SOURCE STREQUAL "fallback")
    message(FATAL_ERROR "Version fallback changed unexpectedly")
endif()
