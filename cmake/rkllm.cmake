# RKLLM is packaged with the RKNN target so the deployed service does not depend
# on a host mount or a board-wide manual installation.
set(COSMO_RKLLM_ROOT "${COSMO_RKNN_ROOT}" CACHE PATH
    "RKLLM Runtime root containing include/rkllm.h and lib/librkllmrt.so")
if(DEFINED ENV{RKLLM_ROOT} AND NOT EXISTS "${COSMO_RKLLM_ROOT}/include/rkllm.h")
    set(COSMO_RKLLM_ROOT "$ENV{RKLLM_ROOT}" CACHE PATH "RKLLM Runtime root" FORCE)
endif()

set(RKLLM_RUNTIME_HEADER "${COSMO_RKLLM_ROOT}/include/rkllm.h")
set(RKLLM_RUNTIME_LIBRARY "${COSMO_RKLLM_ROOT}/lib/librkllmrt.so")
if(NOT EXISTS "${RKLLM_RUNTIME_HEADER}")
    message(FATAL_ERROR "RKLLM header not found: ${RKLLM_RUNTIME_HEADER}")
endif()
if(NOT EXISTS "${RKLLM_RUNTIME_LIBRARY}")
    message(FATAL_ERROR "RKLLM runtime not found: ${RKLLM_RUNTIME_LIBRARY}")
endif()

add_library(rkllmrt SHARED IMPORTED GLOBAL)
set_target_properties(rkllmrt PROPERTIES
    IMPORTED_LOCATION "${RKLLM_RUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${COSMO_RKLLM_ROOT}/include"
)
