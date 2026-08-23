# RKNN Runtime is supplied by the pinned offline SDK bundle. Keep it external
# to the repository while copying the validated runtime into deployment
# packages so the target never falls back to an incompatible system library.
set(COSMO_RKNN_ROOT "" CACHE PATH "RKNN Runtime root containing include/ and lib/")
if(NOT COSMO_RKNN_ROOT AND DEFINED ENV{RKNN_ROOT})
    set(COSMO_RKNN_ROOT "$ENV{RKNN_ROOT}" CACHE PATH "RKNN Runtime root" FORCE)
endif()

if(NOT COSMO_RKNN_ROOT)
    message(FATAL_ERROR "COSMO_RKNN_ROOT is required for the RKNN backend")
endif()

set(RKNN_RUNTIME_HEADER "${COSMO_RKNN_ROOT}/include/rknn_api.h")
set(RKNN_RUNTIME_LIBRARY "${COSMO_RKNN_ROOT}/lib/librknnrt.so")
if(NOT EXISTS "${RKNN_RUNTIME_HEADER}")
    message(FATAL_ERROR "RKNN header not found: ${RKNN_RUNTIME_HEADER}")
endif()
if(NOT EXISTS "${RKNN_RUNTIME_LIBRARY}")
    message(FATAL_ERROR "RKNN runtime not found: ${RKNN_RUNTIME_LIBRARY}")
endif()

add_library(rknnrt SHARED IMPORTED GLOBAL)
set_target_properties(rknnrt PROPERTIES
    IMPORTED_LOCATION "${RKNN_RUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${COSMO_RKNN_ROOT}/include"
)
