cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
endif()
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

if(" ${CMAKE_SOURCE_DIR}" STREQUAL " ${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "FATAL: In-source builds are not allowed. You should create a separate directory for build files.")
endif()

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation Directory")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
endif()

set(COSMO_TARGET_ARCH "aarch64" CACHE STRING "Target architecture: aarch64 or x86_64")
set_property(CACHE COSMO_TARGET_ARCH PROPERTY STRINGS aarch64 x86_64)
if(NOT COSMO_TARGET_ARCH STREQUAL "aarch64" AND NOT COSMO_TARGET_ARCH STREQUAL "x86_64")
    message(FATAL_ERROR "Unsupported COSMO_TARGET_ARCH=${COSMO_TARGET_ARCH}. Expected aarch64 or x86_64.")
endif()

if(COSMO_TARGET_ARCH STREQUAL "aarch64" AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/toolchains/aarch64-linux.toolchain.cmake"
        CACHE FILEPATH "aarch64 linux cross toolchain")
endif()

project(Cosmo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#Generate compile_commands.json for static analysis tools(clang - tidy, IDE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#Baseline : suppress noisy warnings from third - party headers(global scope)
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Wno-deprecated-declarations -Wno-ignored-attributes")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -Wno-ignored-attributes")

#Strict warnings applied only to project OBJECT libraries(not third - party)
set(COSMO_STRICT_WARNINGS
    -Wall
    -Wextra
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wpedantic
    # -Wconversion
    # -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
)

option(BUILD_TESTS "Build unit tests" OFF)
option(COSMO_ENABLE_COVERAGE "Enable gcov code-coverage instrumentation for the test build (adds --coverage; defines the 'coverage' lcov target)" OFF)
if(BUILD_TESTS AND COSMO_ENABLE_COVERAGE)
    message(STATUS "Enabling code coverage for tests")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

option(COSMO_DEV_MODE "Development mode: disable watchdog and other production-only features" OFF)
if(COSMO_DEV_MODE)
    message(STATUS "Development mode enabled (watchdog disabled)")
    add_compile_definitions(COSMO_DEV_MODE)
endif()

#-- Backend selection(mutually exclusive) --
option(COSMO_NN_USE_SOPHON_BACKEND "Enable Sophon TPU backend" ON)
option(COSMO_NN_USE_CPU_BACKEND "Enable CPU inference backend (ONNX Runtime)" OFF)
option(COSMO_MODEL_GUARD_DEPENDENCY_BOOTSTRAP_ONLY
       "Configure only the AArch64 OpenSSL producer needed by the protected build" OFF)

if(COSMO_NN_USE_SOPHON_BACKEND AND COSMO_NN_USE_CPU_BACKEND)
    message(FATAL_ERROR "COSMO_NN_USE_SOPHON_BACKEND and COSMO_NN_USE_CPU_BACKEND are mutually exclusive")
endif()
if(NOT COSMO_NN_USE_SOPHON_BACKEND AND NOT COSMO_NN_USE_CPU_BACKEND)
    message(FATAL_ERROR "At least one NN backend must be enabled")
endif()

if(COSMO_MODEL_GUARD_DEPENDENCY_BOOTSTRAP_ONLY)
    if(NOT COSMO_TARGET_ARCH STREQUAL "aarch64" OR
       NOT COSMO_NN_USE_SOPHON_BACKEND OR
       COSMO_NN_USE_CPU_BACKEND)
        message(FATAL_ERROR
            "The Model Guard dependency bootstrap is valid only for the "
            "Sophon/AArch64 configuration")
    endif()
endif()

# Media acceleration is selected independently from inference. Defaults keep
# existing builds unchanged while allowing a software media pipeline to be
# paired with another inference backend.
set(_cosmo_media_sophon_default OFF)
set(_cosmo_media_cpu_default OFF)
if(COSMO_NN_USE_SOPHON_BACKEND)
    set(_cosmo_media_sophon_default ON)
else()
    set(_cosmo_media_cpu_default ON)
endif()

option(COSMO_MEDIA_USE_SOPHON_BACKEND "Enable Sophon hardware media backend"
       ${_cosmo_media_sophon_default})
option(COSMO_MEDIA_USE_CPU_BACKEND "Enable FFmpeg software media backend"
       ${_cosmo_media_cpu_default})

set(_cosmo_media_backend_count 0)
foreach(_media_backend_option
        COSMO_MEDIA_USE_SOPHON_BACKEND
        COSMO_MEDIA_USE_CPU_BACKEND)
    if(${_media_backend_option})
        math(EXPR _cosmo_media_backend_count "${_cosmo_media_backend_count} + 1")
    endif()
endforeach()
if(NOT _cosmo_media_backend_count EQUAL 1)
    message(FATAL_ERROR "Exactly one media backend must be enabled: Sophon or CPU")
endif()
if(COSMO_MEDIA_USE_SOPHON_BACKEND AND NOT COSMO_NN_USE_SOPHON_BACKEND)
    message(FATAL_ERROR "Sophon media requires the Sophon device runtime")
endif()

if(COSMO_MEDIA_USE_SOPHON_BACKEND)
    add_compile_definitions(COSMO_MEDIA_USE_SOPHON_BACKEND)
else()
    add_compile_definitions(COSMO_MEDIA_USE_CPU_BACKEND)
endif()

if(COSMO_MODEL_GUARD_DEPENDENCY_BOOTSTRAP_ONLY)
    set(COSMO_ENABLE_OPENH264 OFF)
    set(COSMO_OPENH264_USE_ASM OFF)
elseif(COSMO_MEDIA_USE_CPU_BACKEND)
    set(COSMO_ENABLE_OPENH264 ON)
    set(COSMO_OPENH264_USE_ASM OFF)
else()
    set(COSMO_ENABLE_OPENH264 OFF)
    set(COSMO_OPENH264_USE_ASM OFF)
endif()

if(COSMO_MODEL_GUARD_DEPENDENCY_BOOTSTRAP_ONLY)
    set(COSMO_MODEL_GUARD OFF)
elseif(COSMO_NN_USE_SOPHON_BACKEND)
    set(COSMO_MODEL_GUARD ON)
else()
    set(COSMO_MODEL_GUARD OFF)
endif()

# The production bootstrap embeds the public half of the release signing key.
# It is separate from Model Guard device-certificate verification.
set(COSMO_RELEASE_PUBLIC_KEY_OBJECT "" CACHE FILEPATH
    "Absolute path to the controlled AArch64 release public-key object")
set(COSMO_MODEL_GUARD_BUILD_PROFILE "public-runtime" CACHE STRING
    "Model Guard build profile: public-runtime or production-release")
set(COSMO_EDGE_SOURCE_COMMIT "" CACHE STRING
    "Exact lower-case Edge commit embedded in a SOURCE package identity")
set_property(CACHE COSMO_MODEL_GUARD_BUILD_PROFILE PROPERTY STRINGS
    public-runtime production-release)
option(COSMO_REQUIRE_RELEASE_BOOTSTRAP
       "Fail configuration unless the production release bootstrap can be linked" OFF)

set(COSMO_MODEL_GUARD_BUILD_PROFILES public-runtime production-release)
if(NOT COSMO_MODEL_GUARD_BUILD_PROFILE IN_LIST
       COSMO_MODEL_GUARD_BUILD_PROFILES)
    message(FATAL_ERROR
        "Unsupported COSMO_MODEL_GUARD_BUILD_PROFILE="
        "${COSMO_MODEL_GUARD_BUILD_PROFILE}. Expected public-runtime or "
        "production-release.")
endif()
if(NOT COSMO_MODEL_GUARD_DEPENDENCY_BOOTSTRAP_ONLY)
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "public-runtime")
        if(COSMO_REQUIRE_RELEASE_BOOTSTRAP)
            message(FATAL_ERROR
                "COSMO_MODEL_GUARD_BUILD_PROFILE=public-runtime conflicts with "
                "COSMO_REQUIRE_RELEASE_BOOTSTRAP=ON")
        endif()
        if(NOT "${COSMO_RELEASE_PUBLIC_KEY_OBJECT}" STREQUAL "")
            message(FATAL_ERROR
                "COSMO_RELEASE_PUBLIC_KEY_OBJECT is valid only with "
                "COSMO_MODEL_GUARD_BUILD_PROFILE=production-release")
        endif()
    elseif(NOT COSMO_REQUIRE_RELEASE_BOOTSTRAP)
        message(FATAL_ERROR
            "COSMO_MODEL_GUARD_BUILD_PROFILE=production-release requires "
            "COSMO_REQUIRE_RELEASE_BOOTSTRAP=ON")
    endif()
endif()

if(COSMO_MODEL_GUARD)
    set(COSMO_MODEL_GUARD_SDK_ROOT
        "${CMAKE_SOURCE_DIR}/prebuild/model-guard-v2"
        CACHE PATH "Verified Cosmo Model Guard v2 SDK root")
    get_filename_component(COSMO_MODEL_GUARD_SDK_ROOT
        "${COSMO_MODEL_GUARD_SDK_ROOT}" ABSOLUTE)
    set(MODEL_GUARD_V2_HEADER
        "${COSMO_MODEL_GUARD_SDK_ROOT}/include/cosmo_model_guard_v2.h")
    set(MODEL_GUARD_V2_LIBRARY
        "${COSMO_MODEL_GUARD_SDK_ROOT}/lib/libcosmo_model_guard.so.2.0.0")
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        set(MODEL_GUARD_V2_PROVISION_TOOL
            "${COSMO_MODEL_GUARD_SDK_ROOT}/bin/cosmo-model-provision")
    endif()
    set(MODEL_GUARD_V2_ABI_MANIFEST
        "${COSMO_MODEL_GUARD_SDK_ROOT}/share/cosmo-model-guard/cmg_v2_abi.json")
    set(MODEL_GUARD_V2_DEPENDENCY_MANIFEST
        "${COSMO_MODEL_GUARD_SDK_ROOT}/share/cosmo-model-guard/cmg_v2_dependencies.json")
    set(MODEL_GUARD_V2_SDK_RELEASE_MANIFEST
        "${COSMO_MODEL_GUARD_SDK_ROOT}/share/cosmo-model-guard/sdk-release.env")
    set(MODEL_GUARD_V2_TEST_FIXTURE_MARKER
        "${COSMO_MODEL_GUARD_SDK_ROOT}/share/cosmo-model-guard/TEST_FIXTURE_DO_NOT_DEPLOY")

    # The SDK admission gate intentionally runs before the ExternalProject
    # declarations below.  A protected build therefore requires the exact
    # already-built dependency inputs used by the canonical Sophon workflow;
    # it never guesses versions or silently falls back to host libraries.
    set(COSMO_MODEL_GUARD_OPENSSL_INCLUDE_DIR
        "${CMAKE_BINARY_DIR}/thirdparty_install/openssl/include")
    set(COSMO_MODEL_GUARD_LIBCRYPTO
        "${CMAKE_BINARY_DIR}/thirdparty_install/openssl/lib/libcrypto.so.3")
    set(COSMO_MODEL_GUARD_SOPHON_INCLUDE_DIR
        "${CMAKE_SOURCE_DIR}/3rd/libsophon-0.4.11/include")
    set(COSMO_MODEL_GUARD_SOPHON_LIBRARY_DIR
        "${CMAKE_SOURCE_DIR}/3rd/libsophon-0.4.11/lib")
    foreach(MODEL_GUARD_DEPENDENCY_DIRECTORY IN ITEMS
            COSMO_MODEL_GUARD_OPENSSL_INCLUDE_DIR
            COSMO_MODEL_GUARD_SOPHON_INCLUDE_DIR
            COSMO_MODEL_GUARD_SOPHON_LIBRARY_DIR)
        if(NOT IS_DIRECTORY "${${MODEL_GUARD_DEPENDENCY_DIRECTORY}}")
            message(FATAL_ERROR
                "Protected Sophon configuration requires the existing exact "
                "dependency directory ${MODEL_GUARD_DEPENDENCY_DIRECTORY}="
                "${${MODEL_GUARD_DEPENDENCY_DIRECTORY}}. Run the canonical "
                "third-party build prerequisite first; no host fallback is allowed.")
        endif()
    endforeach()
    foreach(MODEL_GUARD_DEPENDENCY_INPUT IN ITEMS
            COSMO_MODEL_GUARD_LIBCRYPTO
            COSMO_MODEL_GUARD_SOPHON_INCLUDE_DIR
            COSMO_MODEL_GUARD_SOPHON_LIBRARY_DIR)
        if(MODEL_GUARD_DEPENDENCY_INPUT MATCHES "_DIR$")
            continue()
        endif()
        if(NOT EXISTS "${${MODEL_GUARD_DEPENDENCY_INPUT}}" OR
           IS_DIRECTORY "${${MODEL_GUARD_DEPENDENCY_INPUT}}")
            message(FATAL_ERROR
                "Protected Sophon configuration requires the existing exact "
                "dependency input ${MODEL_GUARD_DEPENDENCY_INPUT}="
                "${${MODEL_GUARD_DEPENDENCY_INPUT}}. Run the canonical "
                "third-party build prerequisite first; no host fallback is allowed.")
        endif()
    endforeach()

    set(COSMO_PYTHON3_EXECUTABLE "/usr/bin/python3")
    if(NOT EXISTS "${COSMO_PYTHON3_EXECUTABLE}" OR
       IS_DIRECTORY "${COSMO_PYTHON3_EXECUTABLE}" OR
       NOT CMAKE_READELF OR NOT EXISTS "${CMAKE_READELF}" OR
       NOT CMAKE_NM OR NOT EXISTS "${CMAKE_NM}")
        message(FATAL_ERROR
            "The protected Sophon build requires Python 3 and the target "
            "readelf/nm tools to verify the Model Guard v2 SDK")
    endif()

    set(MODEL_GUARD_V2_VERIFY_ARGUMENTS
        --admission-profile "${COSMO_MODEL_GUARD_BUILD_PROFILE}"
        --sdk-root "${COSMO_MODEL_GUARD_SDK_ROOT}"
        --snapshot-base "${CMAKE_BINARY_DIR}/verified-inputs/model-guard-v2"
        --readelf "${CMAKE_READELF}"
        --nm "${CMAKE_NM}"
        --openssl-include-dir "${COSMO_MODEL_GUARD_OPENSSL_INCLUDE_DIR}"
        --libcrypto "${COSMO_MODEL_GUARD_LIBCRYPTO}"
        --sophon-include-dir "${COSMO_MODEL_GUARD_SOPHON_INCLUDE_DIR}"
        --sophon-library-dir "${COSMO_MODEL_GUARD_SOPHON_LIBRARY_DIR}"
        --expected-profile "v2-only")
    if(EXISTS "${MODEL_GUARD_V2_TEST_FIXTURE_MARKER}")
        message(FATAL_ERROR
            "A marked Model Guard test fixture cannot satisfy either public-runtime "
            "or production-release SDK admission")
    endif()

    set(MODEL_GUARD_V2_REVERIFY_DEPENDS
        "${MODEL_GUARD_V2_HEADER}"
        "${MODEL_GUARD_V2_LIBRARY}"
        "${MODEL_GUARD_V2_ABI_MANIFEST}"
        "${MODEL_GUARD_V2_DEPENDENCY_MANIFEST}"
        "${MODEL_GUARD_V2_SDK_RELEASE_MANIFEST}"
        "${COSMO_MODEL_GUARD_OPENSSL_INCLUDE_DIR}/openssl/opensslv.h"
        "${COSMO_MODEL_GUARD_LIBCRYPTO}"
        "${COSMO_MODEL_GUARD_SOPHON_INCLUDE_DIR}/bmruntime_interface.h"
        "${COSMO_MODEL_GUARD_SOPHON_LIBRARY_DIR}/libbmrt.so"
        "${COSMO_MODEL_GUARD_SOPHON_LIBRARY_DIR}/libbmrt.so.1.0"
        "${COSMO_MODEL_GUARD_SOPHON_LIBRARY_DIR}/libbmlib.so"
        "${COSMO_MODEL_GUARD_SOPHON_LIBRARY_DIR}/libbmlib.so.0"
        "${CMAKE_SOURCE_DIR}/scripts/release_updater.py"
        "${CMAKE_SOURCE_DIR}/scripts/verify_model_guard_v2_sdk.py")
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        list(APPEND MODEL_GUARD_V2_REVERIFY_DEPENDS
            "${MODEL_GUARD_V2_PROVISION_TOOL}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${MODEL_GUARD_V2_REVERIFY_DEPENDS})

    execute_process(
        COMMAND "${COSMO_PYTHON3_EXECUTABLE}" -I -B
                "${CMAKE_SOURCE_DIR}/scripts/verify_model_guard_v2_sdk.py"
                ${MODEL_GUARD_V2_VERIFY_ARGUMENTS}
        RESULT_VARIABLE MODEL_GUARD_V2_VERIFY_RESULT
        OUTPUT_VARIABLE MODEL_GUARD_V2_VERIFY_OUTPUT
        ERROR_VARIABLE MODEL_GUARD_V2_VERIFY_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT MODEL_GUARD_V2_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "COSMO_MODEL_GUARD requires one complete ABI-major-2 SDK; "
            "mixed, missing, legacy, or unverified inputs are forbidden.\n"
            "${MODEL_GUARD_V2_VERIFY_ERROR}")
    endif()

    string(REGEX MATCH "(^|\n)verified_sdk_root=([^\n]+)"
        MODEL_GUARD_V2_VERIFIED_ROOT_LINE "${MODEL_GUARD_V2_VERIFY_OUTPUT}")
    set(MODEL_GUARD_V2_VERIFIED_SDK_ROOT "${CMAKE_MATCH_2}")
    if(NOT IS_ABSOLUTE "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}" OR
       NOT EXISTS "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}")
        message(FATAL_ERROR "Model Guard verifier did not return a valid private SDK snapshot")
    endif()
    set(MODEL_GUARD_V2_HEADER
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/include/cosmo_model_guard_v2.h")
    set(MODEL_GUARD_V2_LIBRARY
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/lib/libcosmo_model_guard.so.2.0.0")
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        set(MODEL_GUARD_V2_PROVISION_TOOL
            "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/bin/cosmo-model-provision")
    endif()
    set(MODEL_GUARD_V2_ABI_MANIFEST
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/share/cosmo-model-guard/cmg_v2_abi.json")
    set(MODEL_GUARD_V2_DEPENDENCY_MANIFEST
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/share/cosmo-model-guard/cmg_v2_dependencies.json")
    set(MODEL_GUARD_V2_SDK_RELEASE_MANIFEST
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/share/cosmo-model-guard/sdk-release.env")
    set(COSMO_MODEL_GUARD_VERIFIED_OPENSSL_ROOT
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/thirdparty/openssl")
    set(COSMO_VERIFIED_SOPHON_ROOT
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/thirdparty/sophon")
    foreach(MODEL_GUARD_VERIFIED_DEPENDENCY_PATH IN ITEMS
            "${COSMO_MODEL_GUARD_VERIFIED_OPENSSL_ROOT}/include/openssl/opensslv.h"
            "${COSMO_MODEL_GUARD_VERIFIED_OPENSSL_ROOT}/lib/libcrypto.so.3"
            "${COSMO_VERIFIED_SOPHON_ROOT}/include/bmruntime_interface.h"
            "${COSMO_VERIFIED_SOPHON_ROOT}/lib/libbmrt.so"
            "${COSMO_VERIFIED_SOPHON_ROOT}/lib/libbmrt.so.1.0"
            "${COSMO_VERIFIED_SOPHON_ROOT}/lib/libbmlib.so"
            "${COSMO_VERIFIED_SOPHON_ROOT}/lib/libbmlib.so.0")
        if(NOT EXISTS "${MODEL_GUARD_VERIFIED_DEPENDENCY_PATH}" OR
           IS_DIRECTORY "${MODEL_GUARD_VERIFIED_DEPENDENCY_PATH}")
            message(FATAL_ERROR
                "Model Guard verifier returned an incomplete dependency snapshot: "
                "${MODEL_GUARD_VERIFIED_DEPENDENCY_PATH}")
        endif()
    endforeach()
    # This target intentionally has no output stamp: every requested build
    # reopens and revalidates the compatibility set before any consumer can
    # link or package it. DEPENDS also gives normal build systems precise
    # invalidation when a controlled input changes.
    set(MODEL_GUARD_V2_REVERIFY_ARGUMENTS
        ${MODEL_GUARD_V2_VERIFY_ARGUMENTS}
        --expected-snapshot-root "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}")
    add_custom_target(cosmo_model_guard_v2_reverify ALL
        COMMAND "${COSMO_PYTHON3_EXECUTABLE}" -I -B
                "${CMAKE_SOURCE_DIR}/scripts/verify_model_guard_v2_sdk.py"
                ${MODEL_GUARD_V2_REVERIFY_ARGUMENTS}
        DEPENDS ${MODEL_GUARD_V2_REVERIFY_DEPENDS}
        COMMENT "Re-verifying the Model Guard v2 SDK compatibility set"
        VERBATIM)

    add_library(cosmo_model_guard_v2 SHARED IMPORTED GLOBAL)
    set_target_properties(cosmo_model_guard_v2 PROPERTIES
        IMPORTED_LOCATION "${MODEL_GUARD_V2_LIBRARY}"
        IMPORTED_SONAME "libcosmo_model_guard.so.2"
        INTERFACE_INCLUDE_DIRECTORIES "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/include"
    )
    message(STATUS "Verified Model Guard v2 SDK:\n${MODEL_GUARD_V2_VERIFY_OUTPUT}")
    message(STATUS
        "Model Guard build profile: ${COSMO_MODEL_GUARD_BUILD_PROFILE}")
endif()

# The production bootstrap embeds the release signing public key. Public builds
# emit the installable SOURCE package without this formal OTA entry point.
set(COSMO_HAS_RELEASE_BOOTSTRAP OFF)
if(COSMO_MODEL_GUARD_DEPENDENCY_BOOTSTRAP_ONLY)
    # The bootstrap-only configure pass produces dependency inputs and returns
    # before application targets. Production trust is admitted on the second,
    # protected configure pass.
elseif(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
    if(NOT COSMO_MODEL_GUARD)
        message(FATAL_ERROR
            "COSMO_MODEL_GUARD_BUILD_PROFILE=production-release requires the "
            "protected Sophon build")
    endif()
    if(NOT COSMO_TARGET_ARCH STREQUAL "aarch64" OR
       NOT COSMO_NN_USE_SOPHON_BACKEND)
        message(FATAL_ERROR
            "The production release bootstrap is valid only for Sophon/AArch64")
    endif()
    if("${COSMO_RELEASE_PUBLIC_KEY_OBJECT}" STREQUAL "" OR
       NOT IS_ABSOLUTE "${COSMO_RELEASE_PUBLIC_KEY_OBJECT}")
        message(FATAL_ERROR
            "production-release requires an absolute "
            "COSMO_RELEASE_PUBLIC_KEY_OBJECT")
    endif()
    set(COSMO_RELEASE_PUBLIC_KEY_LINK_OBJECT
        "${COSMO_RELEASE_PUBLIC_KEY_OBJECT}")
    set(COSMO_RELEASE_PYTHON3_EXECUTABLE "/usr/bin/python3")
    if(NOT EXISTS "${COSMO_RELEASE_PYTHON3_EXECUTABLE}" OR
       IS_DIRECTORY "${COSMO_RELEASE_PYTHON3_EXECUTABLE}" OR
       NOT CMAKE_READELF OR NOT EXISTS "${CMAKE_READELF}" OR
       NOT CMAKE_NM OR NOT EXISTS "${CMAKE_NM}")
        message(FATAL_ERROR
            "Release bootstrap configuration requires Python 3 and target readelf/nm")
    endif()
    execute_process(
        COMMAND "${COSMO_RELEASE_PYTHON3_EXECUTABLE}" -I -B
                "${CMAKE_SOURCE_DIR}/scripts/verify_release_public_key_object.py"
                --object "${COSMO_RELEASE_PUBLIC_KEY_LINK_OBJECT}"
                --readelf "${CMAKE_READELF}"
                --nm "${CMAKE_NM}"
        RESULT_VARIABLE COSMO_RELEASE_KEY_VERIFY_RESULT
        OUTPUT_VARIABLE COSMO_RELEASE_KEY_VERIFY_OUTPUT
        ERROR_VARIABLE COSMO_RELEASE_KEY_VERIFY_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT COSMO_RELEASE_KEY_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "The production release public-key object is missing or invalid.\n"
            "${COSMO_RELEASE_KEY_VERIFY_ERROR}")
    endif()
    set(COSMO_RELEASE_KEY_REVERIFY_DEPENDS
        "${COSMO_RELEASE_PUBLIC_KEY_OBJECT}"
        "${COSMO_RELEASE_PUBLIC_KEY_LINK_OBJECT}"
        "${CMAKE_SOURCE_DIR}/scripts/verify_release_public_key_object.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${COSMO_RELEASE_KEY_REVERIFY_DEPENDS})
    add_custom_target(cosmo_release_public_key_reverify ALL
        COMMAND "${COSMO_RELEASE_PYTHON3_EXECUTABLE}" -I -B
                "${CMAKE_SOURCE_DIR}/scripts/verify_release_public_key_object.py"
                --object "${COSMO_RELEASE_PUBLIC_KEY_LINK_OBJECT}"
                --readelf "${CMAKE_READELF}"
                --nm "${CMAKE_NM}"
        DEPENDS ${COSMO_RELEASE_KEY_REVERIFY_DEPENDS}
        COMMENT "Re-verifying the controlled release public-key object"
        VERBATIM)
    set_source_files_properties("${COSMO_RELEASE_PUBLIC_KEY_LINK_OBJECT}" PROPERTIES
        EXTERNAL_OBJECT TRUE)
    set(COSMO_HAS_RELEASE_BOOTSTRAP ON)
    message(STATUS "Verified production release trust anchor:\n${COSMO_RELEASE_KEY_VERIFY_OUTPUT}")
else()
    message(WARNING
        "The SOURCE package (internal profile: public-runtime) does not build "
        "or install cosmo-release-bootstrap and is not a signed production release")
endif()

# CMake target dependencies do not protect direct `cmake --install` or raw
# `cpack` entry paths.  Emit the same fail-closed checks as the first install
# rule so no file is copied before the verified compatibility snapshot (and,
# for production builds, its release-key object) has been reopened.
#
# Bracket-quote every configured command argument independently.  This keeps
# paths containing whitespace literal in the generated install script without
# delegating any part of the command to a shell.
function(cosmo_append_install_command_argument output_name argument)
    set(argument_delimiter "=")
    while("${argument}" MATCHES "]${argument_delimiter}]")
        string(APPEND argument_delimiter "=")
    endwhile()
    set(quoted_argument
        "[${argument_delimiter}[${argument}]${argument_delimiter}]")
    set(${output_name}
        "${${output_name}}\n        ${quoted_argument}"
        PARENT_SCOPE)
endfunction()

if(COSMO_MODEL_GUARD)
    string(CONCAT COSMO_INSTALL_TRUST_REVERIFY_CODE
        "message(STATUS \"Re-verifying Model Guard v2 before installation\")\n"
        "execute_process(\n"
        "    COMMAND")
    foreach(MODEL_GUARD_INSTALL_VERIFY_ARGUMENT IN ITEMS
            "${COSMO_PYTHON3_EXECUTABLE}"
            "-I"
            "-B"
            "${CMAKE_SOURCE_DIR}/scripts/verify_model_guard_v2_sdk.py")
        cosmo_append_install_command_argument(
            COSMO_INSTALL_TRUST_REVERIFY_CODE
            "${MODEL_GUARD_INSTALL_VERIFY_ARGUMENT}")
    endforeach()
    foreach(MODEL_GUARD_INSTALL_VERIFY_ARGUMENT IN LISTS
            MODEL_GUARD_V2_REVERIFY_ARGUMENTS)
        cosmo_append_install_command_argument(
            COSMO_INSTALL_TRUST_REVERIFY_CODE
            "${MODEL_GUARD_INSTALL_VERIFY_ARGUMENT}")
    endforeach()
    string(APPEND COSMO_INSTALL_TRUST_REVERIFY_CODE [=[

    RESULT_VARIABLE model_guard_install_verify_result
    OUTPUT_VARIABLE model_guard_install_verify_output
    ERROR_VARIABLE model_guard_install_verify_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT model_guard_install_verify_result EQUAL 0)
    message(FATAL_ERROR
        "Install-time Model Guard v2 verification failed before any file was copied.\n"
        "${model_guard_install_verify_output}\n"
        "${model_guard_install_verify_error}")
endif()
message(STATUS "Install-time Model Guard v2 verification passed")
]=])

    if(COSMO_HAS_RELEASE_BOOTSTRAP)
        string(APPEND COSMO_INSTALL_TRUST_REVERIFY_CODE
            "\nmessage(STATUS \"Re-verifying release public key before installation\")\n"
            "execute_process(\n"
            "    COMMAND")
        foreach(COSMO_RELEASE_KEY_INSTALL_VERIFY_ARGUMENT IN ITEMS
                "${COSMO_RELEASE_PYTHON3_EXECUTABLE}"
                "-I"
                "-B"
                "${CMAKE_SOURCE_DIR}/scripts/verify_release_public_key_object.py"
                "--object"
                "${COSMO_RELEASE_PUBLIC_KEY_LINK_OBJECT}"
                "--readelf"
                "${CMAKE_READELF}"
                "--nm"
                "${CMAKE_NM}")
            cosmo_append_install_command_argument(
                COSMO_INSTALL_TRUST_REVERIFY_CODE
                "${COSMO_RELEASE_KEY_INSTALL_VERIFY_ARGUMENT}")
        endforeach()
        string(APPEND COSMO_INSTALL_TRUST_REVERIFY_CODE [=[

    RESULT_VARIABLE release_key_install_verify_result
    OUTPUT_VARIABLE release_key_install_verify_output
    ERROR_VARIABLE release_key_install_verify_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT release_key_install_verify_result EQUAL 0)
    message(FATAL_ERROR
        "Install-time release public-key verification failed before any file was copied.\n"
        "${release_key_install_verify_output}\n"
        "${release_key_install_verify_error}")
endif()
message(STATUS "Install-time release public-key verification passed")
]=])
    endif()

    install(CODE "${COSMO_INSTALL_TRUST_REVERIFY_CODE}")
endif()

message(STATUS "=======================")
message(STATUS "Cosmo Build Info:")
message(STATUS "\tSystem:    ${CMAKE_SYSTEM_NAME}")
message(STATUS "\tProcessor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "\tCompiler:  ${CMAKE_CXX_COMPILER}, Version: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "\tToolchain: ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "\tTarget:    ${COSMO_TARGET_ARCH}")

##########################################################
#Third - party Dependencies
##########################################################
set(THIRDPARTY_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/thirdparty_install CACHE PATH "thirdparty install dir")
add_custom_target(third_build)

# Third-party projects must not inherit wall-clock or ephemeral container
# identity in release artifacts. This epoch is also the one admitted by the
# formal Model Guard SDK dependency manifest.
set(COSMO_REPRODUCIBLE_BUILD_EPOCH 1784776233)
set(COSMO_REPRODUCIBLE_BUILD_UTC "2026-07-23 03:10:33")
set(COSMO_REPRODUCIBLE_BUILD_UNAME
    "Linux cosmo-build 0.0.0 #1 SMP ${COSMO_TARGET_ARCH} GNU/Linux")

# Pre-create include directories for all ExternalProject-managed libraries.
# CMake validates INTERFACE_INCLUDE_DIRECTORIES paths at configure time, but
# ExternalProject only installs headers at build time.  Without this, a clean
# build (rm -rf build && ./build.sh) fails because the paths don't exist yet.
# Note: ffmpeg and openh264 are excluded — they are now provided as prebuilt
# binaries under prebuild/ffmpeg/<arch>/ and do not use ExternalProject.
foreach(_tp_subdir fmt glog zlib sqlitecpp uuid uSockets mp4v2 openssl curl mqtt event cryptopp tokenizers)
    file(MAKE_DIRECTORY "${THIRDPARTY_INSTALL_PREFIX}/${_tp_subdir}/include")
endforeach()

include(ExternalProject)

if(COSMO_MODEL_GUARD_DEPENDENCY_BOOTSTRAP_ONLY)
    # A clean protected build cannot verify generated OpenSSL inputs before
    # they exist. The canonical build script configures this narrow
    # producer-only graph first, then reconfigures the same build tree with the
    # full fail-closed Model Guard gate enabled.
    include(cmake/openssl.cmake)
    add_custom_target(cosmo_model_guard_dependency_inputs
        DEPENDS openssl_external)
    return()
endif()

include(cmake/fmt.cmake)
include(cmake/glog.cmake)
include(cmake/zlib.cmake)
include(cmake/sqlitecpp.cmake)
include(cmake/uuid.cmake)
include(cmake/uSockets.cmake)
include(cmake/mp4v2.cmake)
include(cmake/openssl.cmake)
include(cmake/cryptopp.cmake)
if(COSMO_MODEL_GUARD)
    # Only the mutable Guard dependency producer precedes verification.
    add_dependencies(cosmo_model_guard_v2_reverify
        openssl_external)
endif()
include(cmake/curl.cmake)
include(cmake/mqtt.cmake)
include(cmake/event.cmake)
include(cmake/ffmpeg.cmake)
include(cmake/uWebSockets.cmake)
include(cmake/tokenizers.cmake)
if(COSMO_NN_USE_SOPHON_BACKEND)
    include(cmake/device.cmake)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    include(cmake/onnxruntime.cmake)
endif()
include(cmake/pcap.cmake)
include(cmake/srs.cmake)
#Header - only third - party libraries
add_library(nlohmann INTERFACE)
target_include_directories(nlohmann SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/3rd/include)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/3rd/include)

add_library(eigen3 INTERFACE)
target_include_directories(eigen3 SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/3rd/eigen3-3.2.92)

# Minimal common dependencies shared by nearly all OBJECT libraries.
# Provides SYSTEM include directories for fmt, glog and nlohmann-json,
# which suppresses compiler warnings from these ubiquitous third-party headers.
# Each module declares its own additional third-party deps explicitly.
add_library(cosmo_common INTERFACE)
target_link_libraries(cosmo_common INTERFACE fmt glog nlohmann)

##########################################################
#OBJECT Libraries(per - module incremental compilation)
##########################################################

#-- - Layer 0 : Foundation -- -
add_subdirectory(src/util)

add_subdirectory(src/db)

add_subdirectory(src/platform)

add_subdirectory(src/mem)

add_subdirectory(src/nn)

#-- - Layer 1 : Infrastructure -- -
add_subdirectory(src/media)

add_subdirectory(src/infer)

add_subdirectory(src/network)

#-- - Layer 2 : Business Logic -- -
add_subdirectory(src/linkage)

add_subdirectory(src/service)

add_subdirectory(src/flow)

#-- - Layer 3 : Interface -- -
add_subdirectory(src/api)

#-- - App Entry -- -
file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS "src/app/*.h" "src/app/*.cc")

#All OBJECT library targets(grouped by dependency layer)
#Note : cosmo_nn is excluded — it uses its own compile options(see src/nn/CMakeLists.txt)
set(ALL_OBJECT_LIBS
#Layer 0 — Foundation(no upward dependencies)
    cosmo_util cosmo_db cosmo_platform cosmo_mem
#Layer 1 — Infrastructure
    cosmo_media cosmo_infer cosmo_network
#Layer 2 — Business(linkage depends on service - layer DTO types)
    cosmo_linkage cosmo_service cosmo_flow
#Layer 3 — Interface
    cosmo_api
)

#Apply strict compiler warnings and third-party build ordering to all project OBJECT libraries.
#OBJECT libraries do NOT transitively inherit build-order from IMPORTED targets,
#so we must explicitly add_dependencies to guarantee ExternalProject headers are installed first.
foreach(lib ${ALL_OBJECT_LIBS})
    target_compile_options(${lib} PRIVATE ${COSMO_STRICT_WARNINGS})
    add_dependencies(${lib} third_build)
    if(COSMO_MODEL_GUARD)
        add_dependencies(${lib} cosmo_model_guard_v2_reverify)
    endif()
endforeach()
if(COSMO_MODEL_GUARD)
    add_dependencies(cosmo_nn cosmo_model_guard_v2_reverify)
endif()

#Collect all OBJECT outputs for linking
set(ALL_OBJECTS
    $<TARGET_OBJECTS:cosmo_util>
    $<TARGET_OBJECTS:cosmo_db>
    $<TARGET_OBJECTS:cosmo_platform>
    $<TARGET_OBJECTS:cosmo_mem>
    $<TARGET_OBJECTS:cosmo_linkage>
    $<TARGET_OBJECTS:cosmo_media>
    $<TARGET_OBJECTS:cosmo_infer>
    $<TARGET_OBJECTS:cosmo_network>
    $<TARGET_OBJECTS:cosmo_service>
    $<TARGET_OBJECTS:cosmo_flow>
    $<TARGET_OBJECTS:cosmo_api>
    $<TARGET_OBJECTS:cosmo_nn>
)

#Common link libraries(shared by main executable and tests)
set(COMMON_LIBS
    stdc++fs fmt glog cryptopp SQLiteCpp sqlite3 uSockets uuid z pcap
    openssl_ssl openssl_crypto curl
    event_core event_extra event
    mqtt_c mqtt_cs
    ffmpeg_avcodec ffmpeg_avdevice ffmpeg_avfilter
    ffmpeg_avformat ffmpeg_avutil ffmpeg_swresample ffmpeg_swscale
    mp4v2
    tokenizers_cpp tokenizers_c
    pthread dl m
)

##########################################################
# Embedded-key first-release bootstrap
##########################################################
set(COSMO_RELEASE_BOOTSTRAP_VERIFIER_SOURCE
    ${CMAKE_SOURCE_DIR}/src/bootstrap/ReleaseBootstrapVerifier.cc)

if(COSMO_HAS_RELEASE_BOOTSTRAP)
    add_executable(cosmo-release-bootstrap
        ${CMAKE_SOURCE_DIR}/src/bootstrap/main.cc
        ${CMAKE_SOURCE_DIR}/src/bootstrap/ReleaseBootstrap.cc
        ${COSMO_RELEASE_BOOTSTRAP_VERIFIER_SOURCE}
        ${COSMO_RELEASE_PUBLIC_KEY_LINK_OBJECT})
    add_dependencies(cosmo-release-bootstrap
        third_build
        cosmo_release_public_key_reverify)
    if(COSMO_MODEL_GUARD)
        add_dependencies(cosmo-release-bootstrap
            cosmo_model_guard_v2_reverify)
    endif()
    target_include_directories(cosmo-release-bootstrap PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(cosmo-release-bootstrap PRIVATE ${COSMO_STRICT_WARNINGS})
    target_link_libraries(cosmo-release-bootstrap PRIVATE openssl_crypto)
    target_link_options(cosmo-release-bootstrap PRIVATE
        -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
        -Wl,--enable-new-dtags)
    # Both installed copies use a private sibling lib directory.  In
    # particular, the stable factory copy must not traverse the mutable
    # install-root lib facade while first-release migration is in progress.
    set_target_properties(cosmo-release-bootstrap PROPERTIES
        BUILD_RPATH "${OPENSSL_INSTALL_DIR}/lib"
        BUILD_RPATH_USE_ORIGIN FALSE
        INSTALL_RPATH "\$ORIGIN/../lib"
        INSTALL_RPATH_USE_LINK_PATH FALSE)
endif()

if(BUILD_TESTS)
    # This explicit, non-installed target uses only the RFC 8032 public fixture.
    # It is intentionally separate from cosmo-tests and from the production
    # trust-anchor target so test material can never satisfy the package gate.
    add_executable(cosmo-release-bootstrap-verifier-tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/test/ReleaseBootstrapVerifierStandaloneTest.cc
        ${CMAKE_SOURCE_DIR}/test/fixtures/ReleaseBootstrapTestKey.cc
        ${COSMO_RELEASE_BOOTSTRAP_VERIFIER_SOURCE})
    add_dependencies(cosmo-release-bootstrap-verifier-tests third_build)
    if(COSMO_MODEL_GUARD)
        add_dependencies(cosmo-release-bootstrap-verifier-tests
            cosmo_model_guard_v2_reverify)
    endif()
    target_include_directories(cosmo-release-bootstrap-verifier-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(cosmo-release-bootstrap-verifier-tests PRIVATE
        ${COSMO_STRICT_WARNINGS})
    target_link_libraries(cosmo-release-bootstrap-verifier-tests PRIVATE openssl_crypto)
    set_target_properties(cosmo-release-bootstrap-verifier-tests PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH ON)

    # Compile the complete bootstrap/migration implementation for AArch64 even
    # when controlled production trust objects are unavailable.  The public
    # RFC 8032 fixture key is confined to this explicitly named, non-installed,
    # EXCLUDE_FROM_ALL target and can never satisfy COSMO_HAS_RELEASE_BOOTSTRAP
    # or any package/install dependency.
    add_executable(cosmo-release-bootstrap-test-fixture EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/src/bootstrap/main.cc
        ${CMAKE_SOURCE_DIR}/src/bootstrap/ReleaseBootstrap.cc
        ${COSMO_RELEASE_BOOTSTRAP_VERIFIER_SOURCE}
        ${CMAKE_SOURCE_DIR}/test/fixtures/ReleaseBootstrapTestKey.cc)
    add_dependencies(cosmo-release-bootstrap-test-fixture third_build)
    if(COSMO_MODEL_GUARD)
        add_dependencies(cosmo-release-bootstrap-test-fixture
            cosmo_model_guard_v2_reverify)
    endif()
    target_include_directories(cosmo-release-bootstrap-test-fixture PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_options(cosmo-release-bootstrap-test-fixture PRIVATE
        ${COSMO_STRICT_WARNINGS})
    target_link_libraries(cosmo-release-bootstrap-test-fixture PRIVATE openssl_crypto)
    target_link_options(cosmo-release-bootstrap-test-fixture PRIVATE
        -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
        -Wl,--enable-new-dtags)
    set_target_properties(cosmo-release-bootstrap-test-fixture PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH ON)
endif()

#Backend - specific link libraries
if(COSMO_NN_USE_SOPHON_BACKEND)
    list(APPEND COMMON_LIBS bmlib bmrt bmcv bmvd bmvenc)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    list(APPEND COMMON_LIBS onnxruntime)
endif()

##########################################################
#Main Executable
##########################################################
set(EXECUTABLE_NAME "cosmo-engine")

add_executable(${EXECUTABLE_NAME} ${APP_SRC} ${ALL_OBJECTS})
target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
# Third-party headers come from IMPORTED targets via COMMON_LIBS + header-only libs
target_link_libraries(${EXECUTABLE_NAME} PRIVATE nlohmann stb uWebSockets)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_link_directories(${EXECUTABLE_NAME} PRIVATE ${DEVICE_LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
endif()
if(COSMO_MODEL_GUARD)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE COSMO_HAS_MODEL_GUARD=1)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE cosmo_model_guard_v2)
    add_dependencies(${EXECUTABLE_NAME} cosmo_model_guard_v2_reverify)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE
        COSMO_NN_USE_CPU_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_ONNX_BACKEND)
endif()
#Build order: OBJECT libs → third_build (explicit, see foreach above)
target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${COMMON_LIBS})

##########################################################
#Unit Tests(optional, cmake - DBUILD_TESTS = ON)
##########################################################
if(BUILD_TESTS)
    file(GLOB TEST_SRC "test/*.cc" "test/*.cpp" "test/mock/*.cc")
    list(FILTER TEST_SRC EXCLUDE REGEX
        ".*ReleaseBootstrapVerifierStandaloneTest\\.cc$")

    #App sources without main.cc
    set(APP_SRC_NO_MAIN ${APP_SRC})
    list(FILTER APP_SRC_NO_MAIN EXCLUDE REGEX ".*app/main\\.cc$")

    add_executable(cosmo-tests ${APP_SRC_NO_MAIN} ${ALL_OBJECTS} ${TEST_SRC})
    add_dependencies(cosmo-tests third_build)
    target_include_directories(cosmo-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/test)
    target_precompile_headers(cosmo-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/test/catch_amalgamated.hpp
        ${CMAKE_SOURCE_DIR}/test/catch2/trompeloeil.hpp
    )
    target_link_libraries(cosmo-tests PRIVATE nlohmann stb uWebSockets)
    if(COSMO_NN_USE_SOPHON_BACKEND)
        target_link_directories(cosmo-tests PRIVATE ${DEVICE_LIB_DIR})
        target_compile_definitions(cosmo-tests PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
    endif()
    if(COSMO_MODEL_GUARD)
        target_compile_definitions(cosmo-tests PRIVATE COSMO_HAS_MODEL_GUARD=1)
        target_link_libraries(cosmo-tests PRIVATE cosmo_model_guard_v2)
        add_dependencies(cosmo-tests cosmo_model_guard_v2_reverify)
    endif()
    if(COSMO_NN_USE_CPU_BACKEND)
        target_compile_definitions(cosmo-tests PRIVATE
            COSMO_NN_USE_CPU_BACKEND
            COSMO_NN_USE_HOST_BACKEND
            COSMO_NN_USE_ONNX_BACKEND)
    endif()
    target_link_libraries(cosmo-tests PRIVATE ${COMMON_LIBS})
    if(COSMO_ENABLE_COVERAGE)
        # gcov runtime is only needed when the test binary was instrumented.
        target_link_libraries(cosmo-tests PRIVATE gcov)

        add_custom_target(coverage
            COMMAND lcov --capture --directory . --output-file coverage.info
            COMMAND lcov --remove coverage.info '/usr/*' '*/3rd/*' '*/test/*' '*/thirdparty_install/*' --output-file coverage.info
            COMMAND genhtml coverage.info --output-directory coverage_report
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating coverage report..."
        )
    endif()
endif()




##########################################################
# Web Frontend Build (Vue 3 + Vite)
##########################################################
include(cmake/web_frontend.cmake)

##########################################################
# Install
##########################################################

# Binary
install(TARGETS ${EXECUTABLE_NAME}
    DESTINATION bin
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ GROUP_EXECUTE
)

if(COSMO_HAS_RELEASE_BOOTSTRAP)
    # Keep the signed-release payload member required by the release schema.
    install(TARGETS cosmo-release-bootstrap
        DESTINATION bin
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
    # This second copy is outside every movable facade and remains executable
    # while bin/scripts/lib are being migrated or restored.
    install(TARGETS cosmo-release-bootstrap
        DESTINATION .release-bootstrap/bin
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
endif()

# Nginx configuration (uses system nginx, no custom binary)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/nginx/conf
    DESTINATION bin/nginx_conf
    FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
)

# Web Frontend
install(DIRECTORY ${WEB_BUILD_DIR}/web_unified/dist/
    DESTINATION web
    FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
)

# Data
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/
    DESTINATION files
    FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
    PATTERN "*.json" EXCLUDE
    PATTERN "*.OTF"  EXCLUDE
    PATTERN "netplan" EXCLUDE
    PATTERN "resource" EXCLUDE
    PATTERN "test-video" EXCLUDE
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "*.pyo" EXCLUDE
)
# Font
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/SOURCEHANSANSCN-REGULAR.OTF
    DESTINATION font
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
)

# Resource (models, algorithm, layout, etc. from external repository)
if(DEFINED RESOURCE_DIR AND IS_DIRECTORY "${RESOURCE_DIR}")
    message(STATUS "Resource directory: ${RESOURCE_DIR}")
    install(DIRECTORY ${RESOURCE_DIR}/
        DESTINATION resource
        FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
        PATTERN ".git" EXCLUDE
        PATTERN "model.nn"
            PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN "*.pyo" EXCLUDE
    )
else()
    message(STATUS "No resource directory specified — package will not contain models")
endif()

# Scripts (exclude dev-only tools from device deployment)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/scripts/
    DESTINATION scripts
    FILES_MATCHING
        PATTERN "build*" EXCLUDE
        PATTERN "format_check*" EXCLUDE
        PATTERN "static_analysis*" EXCLUDE
        PATTERN "install-hooks*" EXCLUDE
        PATTERN "pre-commit" EXCLUDE
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN "*.pyo" EXCLUDE
        PATTERN "package_md5_rename.sh" EXCLUDE
        PATTERN "restore-symlinks.sh" EXCLUDE
        PATTERN "sync-source-volume.sh" EXCLUDE
        PATTERN "release_updater.sh" EXCLUDE
        PATTERN "release_health_check.sh" EXCLUDE
        PATTERN "install.sh" EXCLUDE
        PATTERN "start.sh" EXCLUDE
        PATTERN "inte_run_start.sh" EXCLUDE
        PATTERN "source_health_check.sh" EXCLUDE
        PATTERN "source_run_start.sh" EXCLUDE
        PATTERN "*.sh"
            PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE
)

set(_COSMO_SOURCE_PACKAGE OFF)
if(COSMO_MODEL_GUARD AND
   COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "public-runtime")
    set(_COSMO_SOURCE_PACKAGE ON)
endif()
string(LENGTH "${COSMO_EDGE_SOURCE_COMMIT}" _COSMO_EDGE_SOURCE_COMMIT_LENGTH)
if(_COSMO_SOURCE_PACKAGE AND
   (NOT _COSMO_EDGE_SOURCE_COMMIT_LENGTH EQUAL 40 OR
    COSMO_EDGE_SOURCE_COMMIT MATCHES "[^0-9a-f]"))
    message(FATAL_ERROR
        "SOURCE packaging requires COSMO_EDGE_SOURCE_COMMIT as an exact "
        "lower-case 40-hex Edge commit")
endif()

# These are signed-release lifecycle entry points, not SOURCE runtime helpers.
# Reinstall them explicitly everywhere except the protected SOURCE package so
# existing production and CPU package contents retain their previous behavior.
if(NOT _COSMO_SOURCE_PACKAGE)
    install(PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/start.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/inte_run_start.sh
        DESTINATION scripts
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_READ GROUP_EXECUTE
    )
endif()

if(_COSMO_SOURCE_PACKAGE)
    # SOURCE-only installation assets. They deliberately do not commission a
    # device or participate in the signed production release transaction.
    install(PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/install-device.sh
        DESTINATION .
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
    install(PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/source_health_check.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/source_run_start.sh
        DESTINATION scripts
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/config/systemd/cosmo-source.service
        DESTINATION share/cosmo-source
        RENAME cosmo.service
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
else()
    # Formal release transaction entry points are not SOURCE installation
    # assets. Keep the controlled production payload unchanged while ensuring
    # an ordinary source build cannot be mistaken for the signed update path.
    install(PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_updater.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_health_check.sh
        DESTINATION scripts
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE
    )
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_updater.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_bootstrap_backend.py
        DESTINATION scripts
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )

    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        # Factory setup installs this stable unit only after the device-bound
        # Guard certificate and the first signed release have been staged.
        install(FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/config/systemd/cosmo.service
            DESTINATION share/cosmo-factory
            PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
        )
    endif()
endif()

if(COSMO_HAS_RELEASE_BOOTSTRAP)
    # Factory bootstrap/recovery never imports through the movable scripts
    # facade.  These stable copies are opened and identity-checked by FD.
    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_updater.py
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_bootstrap_backend.py
        DESTINATION .release-bootstrap/scripts
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
    install(PROGRAMS
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/release_health_check.sh
        DESTINATION .release-bootstrap/scripts
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
    # The verifier's sole private dynamic dependency is resolved from this
    # directory by its exact $ORIGIN/../lib DT_RUNPATH. Copy only the verified
    # SONAME member; a linker alias is not needed at runtime and would widen
    # the factory recovery inventory.
    install(FILES
        ${OPENSSL_CRYPTO_LIB}
        DESTINATION .release-bootstrap/lib
        PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
endif()

# Netplan failsafe config (deployed to scripts/ to keep device-side path unchanged)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/netplan/
    DESTINATION scripts
    FILES_MATCHING
        PATTERN "*.bak"
            PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ
)

##########################################################
# Packaging
##########################################################
file(STRINGS "${CMAKE_SOURCE_DIR}/src/util/Version.h" _ver_major REGEX "kVersionMajor")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/util/Version.h" _ver_minor REGEX "kVersionMinor")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/util/Version.h" _ver_patch REGEX "kVersionPatch")
string(REGEX REPLACE ".*kVersionMajor = ([0-9]+).*" "\\1" VER_MAJOR "${_ver_major}")
string(REGEX REPLACE ".*kVersionMinor = ([0-9]+).*" "\\1" VER_MINOR "${_ver_minor}")
string(REGEX REPLACE ".*kVersionPatch = ([0-9]+).*" "\\1" VER_PATCH "${_ver_patch}")

set(PACKAGE_PREFIX  "cosmo")
set(PACKAGE_VERSION "V${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
set(PACKAGE_NAME    "${PACKAGE_PREFIX}-${PACKAGE_VERSION}")

# Generate and install version.txt
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.txt" "${PACKAGE_VERSION}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/version.txt"
    DESTINATION bin
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
)

# Install the exact verified compatibility member. The linker aliases are
# recreated inside the package; no unversioned host/system library is copied.
if(COSMO_MODEL_GUARD)
    install(FILES "${MODEL_GUARD_V2_LIBRARY}"
        DESTINATION lib
        PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    )
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        install(PROGRAMS "${MODEL_GUARD_V2_PROVISION_TOOL}"
            DESTINATION bin
            RENAME cosmo-model-provision
            PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE
        )
    endif()
    install(FILES "${MODEL_GUARD_V2_HEADER}"
        DESTINATION share/cosmo-model-guard
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
    install(FILES "${MODEL_GUARD_V2_ABI_MANIFEST}"
        DESTINATION share/cosmo-model-guard
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
    install(FILES "${MODEL_GUARD_V2_DEPENDENCY_MANIFEST}"
        DESTINATION share/cosmo-model-guard
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
    install(FILES "${MODEL_GUARD_V2_SDK_RELEASE_MANIFEST}"
        DESTINATION share/cosmo-model-guard
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
    install(CODE [[
        set(model_guard_lib_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib")
        file(CREATE_LINK libcosmo_model_guard.so.2.0.0
             "${model_guard_lib_dir}/libcosmo_model_guard.so.2"
             SYMBOLIC RESULT model_guard_soname_link_result)
        if(NOT model_guard_soname_link_result STREQUAL "0")
            message(FATAL_ERROR
                "Cannot create Model Guard SONAME link: ${model_guard_soname_link_result}")
        endif()
        file(CREATE_LINK libcosmo_model_guard.so.2
             "${model_guard_lib_dir}/libcosmo_model_guard.so"
             SYMBOLIC RESULT model_guard_link_link_result)
        if(NOT model_guard_link_link_result STREQUAL "0")
            message(FATAL_ERROR
                "Cannot create Model Guard linker alias: ${model_guard_link_link_result}")
        endif()
    ]])
else()
    # CPU-only development packages retain the historical empty directory.
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
        DESTINATION .
    )
endif()

if(_COSMO_SOURCE_PACKAGE)
    # This identity travels inside the extracted payload. It deliberately does
    # not contain the outer tar SHA-256, which exists only after CPack has
    # produced the archive and is recorded in the distribution filename.
    set(COSMO_SOURCE_IDENTITY_INSTALL_CODE [=[
        set(source_identity_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
        set(source_identity_engine
            "${source_identity_root}/bin/cosmo-engine")
        set(source_identity_version
            "${source_identity_root}/bin/version.txt")
        set(source_identity_sdk_release
            "${source_identity_root}/share/cosmo-model-guard/sdk-release.env")
        foreach(source_identity_input IN ITEMS
                "${source_identity_engine}"
                "${source_identity_version}"
                "${source_identity_sdk_release}")
            if(NOT EXISTS "${source_identity_input}" OR
               IS_DIRECTORY "${source_identity_input}" OR
               IS_SYMLINK "${source_identity_input}")
                message(FATAL_ERROR
                    "SOURCE build-identity input is missing or unsafe: "
                    "${source_identity_input}")
            endif()
        endforeach()

        file(READ "${source_identity_version}" source_identity_version_text)
        if(NOT source_identity_version_text STREQUAL "@PACKAGE_VERSION@\n")
            message(FATAL_ERROR
                "SOURCE version.txt differs from the configured package version")
        endif()
        file(STRINGS "${source_identity_sdk_release}"
            source_identity_guard_lines
            REGEX "^CMG_SDK_RELEASE_ID=[A-Za-z0-9._+-]+$")
        list(LENGTH source_identity_guard_lines source_identity_guard_count)
        if(NOT source_identity_guard_count EQUAL 1)
            message(FATAL_ERROR
                "SOURCE SDK release manifest has an ambiguous release ID")
        endif()
        list(GET source_identity_guard_lines 0 source_identity_guard_line)
        string(REGEX REPLACE "^CMG_SDK_RELEASE_ID=" ""
            source_identity_guard_release_id
            "${source_identity_guard_line}")
        file(SHA256 "${source_identity_engine}" source_identity_engine_sha256)

        string(CONCAT source_identity_hash_input
            "cosmo-source-build-identity-v1\n"
            "edge_commit=@COSMO_EDGE_SOURCE_COMMIT@\n"
            "version=@PACKAGE_VERSION@\n"
            "guard_release_id=${source_identity_guard_release_id}\n"
            "engine_sha256=${source_identity_engine_sha256}\n")
        string(SHA256 source_identity_build_id
            "${source_identity_hash_input}")
        set(source_identity_directory
            "${source_identity_root}/share/cosmo-source")
        set(source_identity_file
            "${source_identity_directory}/build-identity.env")
        file(MAKE_DIRECTORY "${source_identity_directory}")
        file(WRITE "${source_identity_file}"
            "format=cosmo-source-build-identity-v1\n"
            "edge_commit=@COSMO_EDGE_SOURCE_COMMIT@\n"
            "version=@PACKAGE_VERSION@\n"
            "guard_release_id=${source_identity_guard_release_id}\n"
            "engine_sha256=${source_identity_engine_sha256}\n"
            "build_identity=${source_identity_build_id}\n")
        message(STATUS
            "Installed SOURCE build identity: ${source_identity_build_id}")
    ]=])
    string(CONFIGURE "${COSMO_SOURCE_IDENTITY_INSTALL_CODE}"
        COSMO_SOURCE_IDENTITY_INSTALL_CODE @ONLY)
    install(CODE "${COSMO_SOURCE_IDENTITY_INSTALL_CODE}")
endif()

set(CPACK_PACKAGE_NAME              "${PACKAGE_PREFIX}")
set(CPACK_PACKAGE_VENDOR            "Nanjing Cosmo Wander AI Technology Co., Ltd.")
set(CPACK_PACKAGE_VERSION           "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
set(CPACK_PACKAGE_VERSION_MAJOR     "${VER_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR     "${VER_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH     "${VER_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION       "Cosmo Package")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cosmo - AI Inference Engine")
set(CPACK_PACKAGE_CONTACT           "hello@cosmowander.ai")
set(CPACK_GENERATOR                 "TGZ")
set(CPACK_PACKAGE_FILE_NAME         "${PACKAGE_NAME}")
set(CPACK_OUTPUT_FILE_PREFIX        "${CMAKE_BINARY_DIR}/packages")

include(CPack)

set(COSMO_PACKAGE_TARGETS ${EXECUTABLE_NAME} web_frontend)
if(COSMO_HAS_RELEASE_BOOTSTRAP)
    list(APPEND COSMO_PACKAGE_TARGETS cosmo-release-bootstrap)
endif()
add_custom_target(package_all
    # CPack names are content-addressed only after generation. Clear artifacts
    # from an earlier profile before creating and exporting this build's output.
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${CMAKE_BINARY_DIR}/packages"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_BINARY_DIR}/packages"
    COMMAND cpack
    COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/package_md5_rename.sh"
        "${CMAKE_BINARY_DIR}/packages" "${PACKAGE_NAME}"
        "${COSMO_MODEL_GUARD_BUILD_PROFILE}"
        "${COSMO_REPRODUCIBLE_BUILD_EPOCH}"
    DEPENDS ${COSMO_PACKAGE_TARGETS}
    COMMENT "Creating package with SHA-256 in filename"
)
if(COSMO_MODEL_GUARD)
    add_dependencies(package_all cosmo_model_guard_v2_reverify)
endif()
if(COSMO_HAS_RELEASE_BOOTSTRAP)
    add_dependencies(package_all cosmo_release_public_key_reverify)
endif()
