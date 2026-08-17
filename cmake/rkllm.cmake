# RKLLM is packaged with the RKNN target so the deployed service does not depend
# on a host mount or a board-wide manual installation.
set(COSMO_RKLLM_ROOT "${COSMO_RKNN_ROOT}" CACHE PATH
    "RKLLM Runtime root containing include/rkllm.h and lib/librkllmrt.so")
if(DEFINED ENV{RKLLM_ROOT} AND NOT EXISTS "${COSMO_RKLLM_ROOT}/include/rkllm.h")
    set(COSMO_RKLLM_ROOT "$ENV{RKLLM_ROOT}" CACHE PATH "RKLLM Runtime root" FORCE)
endif()

set(RKLLM_RUNTIME_HEADER "${COSMO_RKLLM_ROOT}/include/rkllm.h")
set(RKLLM_RUNTIME_LIBRARY "${COSMO_RKLLM_ROOT}/lib/librkllmrt.so")
set(RKLLM_RUNTIME_LICENSE "${COSMO_RKLLM_ROOT}/LICENSE")

option(COSMO_RKLLM_REQUIRED "Fail configuration when the RKLLM SDK is unavailable" OFF)
set(COSMO_NN_USE_RKLLM_BACKEND OFF)

if(EXISTS "${RKLLM_RUNTIME_HEADER}" AND EXISTS "${RKLLM_RUNTIME_LIBRARY}" AND
   EXISTS "${RKLLM_RUNTIME_LICENSE}")
    add_library(rkllmrt SHARED IMPORTED GLOBAL)
    set_target_properties(rkllmrt PROPERTIES
        IMPORTED_LOCATION "${RKLLM_RUNTIME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${COSMO_RKLLM_ROOT}/include"
    )
    set(COSMO_NN_USE_RKLLM_BACKEND ON)
    add_compile_definitions(COSMO_NN_USE_RKLLM_BACKEND)
    message(STATUS "RKLLM multimodal backend enabled: ${COSMO_RKLLM_ROOT}")
elseif(COSMO_RKLLM_REQUIRED)
    message(FATAL_ERROR
        "RKLLM SDK is required but incomplete under ${COSMO_RKLLM_ROOT} "
        "(expected include/rkllm.h, lib/librkllmrt.so, and LICENSE)")
else()
    message(STATUS
        "complete RKLLM SDK not found under ${COSMO_RKLLM_ROOT}; "
        "building the RKNN runtime without Qwen3.5 VLM support")
endif()
