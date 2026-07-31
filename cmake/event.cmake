set(EVENT_ORIGINAL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libevent-2.1.12-stable)
set(EVENT_SOURCE_DIR ${CMAKE_BINARY_DIR}/event_source)
set(EVENT_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/event)
set(EVENT_HEADERS ${EVENT_INSTALL_DIR}/include)
set(EVENT_LIB ${EVENT_INSTALL_DIR}/lib/libevent.so)
set(EVENT_CORE_LIB ${EVENT_INSTALL_DIR}/lib/libevent_core.so)
set(EVENT_EXTRA_LIB ${EVENT_INSTALL_DIR}/lib/libevent_extra.so)
set(EVENT_OPENSSL_LIB ${EVENT_INSTALL_DIR}/lib/libevent_openssl.so)
set(EVENT_PTHREADS_LIB ${EVENT_INSTALL_DIR}/lib/libevent_pthreads.so)
set(EVENT_EXTERNAL_DEPENDS openssl_external)
if(COSMO_MODEL_GUARD)
    list(APPEND EVENT_EXTERNAL_DEPENDS cosmo_model_guard_v2_reverify)
endif()

ExternalProject_Add(
    event_external

    SOURCE_DIR ${EVENT_SOURCE_DIR}
    BINARY_DIR ${CMAKE_BINARY_DIR}/event_build

    DOWNLOAD_COMMAND
        ${CMAKE_COMMAND} -E rm -rf <SOURCE_DIR>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${EVENT_ORIGINAL_SOURCE_DIR} <SOURCE_DIR>

    PATCH_COMMAND
        patch --batch --forward -p1
              -i ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libevent-relative-rpath.patch

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${EVENT_INSTALL_DIR}
        -DOPENSSL_ROOT_DIR=${OPENSSL_INSTALL_DIR}
        -DOPENSSL_INCLUDE_DIR=${OPENSSL_HEADERS}
        -DOPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIB}
        -DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIB}
        -DEVENT__LIBRARY_TYPE=SHARED
        -DEVENT__DISABLE_DEBUG_MODE=ON
        -DEVENT__ENABLE_VERBOSE_DEBUG=OFF
        -DEVENT__DISABLE_BENCHMARK=ON
        -DEVENT__DISABLE_TESTS=ON
        -DEVENT__DISABLE_REGRESS=ON
        -DEVENT__DISABLE_SAMPLES=ON
    
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install

    DEPENDS ${EVENT_EXTERNAL_DEPENDS}

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)

add_dependencies(third_build event_external)

add_library(event_core SHARED IMPORTED)
set_target_properties(event_core PROPERTIES
    IMPORTED_LOCATION ${EVENT_CORE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${EVENT_HEADERS}"
)
add_dependencies(event_core event_external)

add_library(event_extra SHARED IMPORTED)
set_target_properties(event_extra PROPERTIES
    IMPORTED_LOCATION ${EVENT_EXTRA_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${EVENT_HEADERS}"
)
add_dependencies(event_extra event_external)

add_library(event_openssl SHARED IMPORTED)
set_target_properties(event_openssl PROPERTIES
    IMPORTED_LOCATION ${EVENT_OPENSSL_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${EVENT_HEADERS}"
)
add_dependencies(event_openssl event_external)

add_library(event_pthreads SHARED IMPORTED)
set_target_properties(event_pthreads PROPERTIES
    IMPORTED_LOCATION ${EVENT_PTHREADS_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${EVENT_HEADERS}"
)
add_dependencies(event_pthreads event_external)

add_library(event SHARED IMPORTED)
set_target_properties(event PROPERTIES
    IMPORTED_LOCATION ${EVENT_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${EVENT_HEADERS}"
)
add_dependencies(event event_external)

install(DIRECTORY ${EVENT_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
    PATTERN "*event*"
    PATTERN "*so*"
)
