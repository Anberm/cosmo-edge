# Rockchip MPP and RGA are supplied by a candidate-bound external sysroot.
# The validated shared libraries are packaged beside CosmoEdge so qualification
# never depends on a silently different target image library.
set(COSMO_ROCKCHIP_MEDIA_ROOT "" CACHE PATH
    "Rockchip media root containing include/rockchip, include/rga, and lib/")
if(NOT COSMO_ROCKCHIP_MEDIA_ROOT AND DEFINED ENV{ROCKCHIP_MEDIA_ROOT})
    set(COSMO_ROCKCHIP_MEDIA_ROOT "$ENV{ROCKCHIP_MEDIA_ROOT}"
        CACHE PATH "Rockchip media root" FORCE)
endif()

if(NOT COSMO_ROCKCHIP_MEDIA_ROOT)
    message(FATAL_ERROR
        "COSMO_ROCKCHIP_MEDIA_ROOT is required for the Rockchip media backend")
endif()

set(ROCKCHIP_MPP_HEADER
    "${COSMO_ROCKCHIP_MEDIA_ROOT}/include/rockchip/rk_mpi.h")
set(ROCKCHIP_RGA_HEADER
    "${COSMO_ROCKCHIP_MEDIA_ROOT}/include/rga/im2d.h")
set(ROCKCHIP_MPP_LIBRARY
    "${COSMO_ROCKCHIP_MEDIA_ROOT}/lib/librockchip_mpp.so")
set(ROCKCHIP_RGA_LIBRARY
    "${COSMO_ROCKCHIP_MEDIA_ROOT}/lib/librga.so")

foreach(_required
        "${ROCKCHIP_MPP_HEADER}"
        "${ROCKCHIP_RGA_HEADER}"
        "${ROCKCHIP_MPP_LIBRARY}"
        "${ROCKCHIP_RGA_LIBRARY}")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Rockchip media artifact not found: ${_required}")
    endif()
endforeach()

add_library(rockchip_mpp SHARED IMPORTED GLOBAL)
set_target_properties(rockchip_mpp PROPERTIES
    IMPORTED_LOCATION "${ROCKCHIP_MPP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${COSMO_ROCKCHIP_MEDIA_ROOT}/include")

add_library(rockchip_rga SHARED IMPORTED GLOBAL)
set_target_properties(rockchip_rga PROPERTIES
    IMPORTED_LOCATION "${ROCKCHIP_RGA_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${COSMO_ROCKCHIP_MEDIA_ROOT}/include")

install(DIRECTORY "${COSMO_ROCKCHIP_MEDIA_ROOT}/lib/"
    DESTINATION lib
    FILES_MATCHING
        PATTERN "librockchip_mpp.so*"
        PATTERN "librga.so*")

set(ROCKCHIP_MEDIA_MANIFEST
    "${COSMO_ROCKCHIP_MEDIA_ROOT}/.cosmo-rockchip-media.json")
if(EXISTS "${ROCKCHIP_MEDIA_MANIFEST}")
    install(FILES "${ROCKCHIP_MEDIA_MANIFEST}"
        DESTINATION share/cosmo/platform
        RENAME rockchip-media-manifest.json)
endif()
