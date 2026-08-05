set(MP4V2_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/mp4v2-2.0.0)
set(MP4V2_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/mp4v2)
set(MP4V2_HEADERS ${MP4V2_INSTALL_DIR}/include)
set(MP4V2_LIB ${MP4V2_INSTALL_DIR}/lib/libmp4v2.so)

set(MP4V2_CONFIGURE_ARGS
    --prefix=${MP4V2_INSTALL_DIR}
    --disable-debug
    --disable-util
    --enable-shared=yes
    --enable-static=no
    CC=${CMAKE_C_COMPILER}
    CXX=${CMAKE_CXX_COMPILER}
)

if(COSMO_TARGET_ARCH STREQUAL "aarch64")
    list(APPEND MP4V2_CONFIGURE_ARGS --host=aarch64-linux-gnu)
endif()

ExternalProject_Add(
    mp4v2_external

    SOURCE_DIR ${MP4V2_SOURCE_DIR}

    # mp4v2 2.0.0 由 automake 1.11 生成, 其 GNUmakefile 中的 GNUmakefile.in
    # 再生成规则硬编码了 automake-1.11; 而 x86 构建镜像只提供 automake 1.16。
    # 构建上下文中 aclocal.m4/configure 的 mtime 若新于 GNUmakefile.in,
    # 该规则就会触发, 导致 "automake-1.11: command not found"。
    # 此处 touch 生成的 autotools 文件, 使再生成规则永不触发。
    PATCH_COMMAND touch <SOURCE_DIR>/GNUmakefile.in <SOURCE_DIR>/aclocal.m4 <SOURCE_DIR>/configure

    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        ${MP4V2_CONFIGURE_ARGS}
    
    BUILD_COMMAND $(MAKE)
    INSTALL_COMMAND $(MAKE) install

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_dependencies(third_build mp4v2_external)

add_library(mp4v2 SHARED IMPORTED)
set_target_properties(mp4v2 PROPERTIES
    IMPORTED_LOCATION ${MP4V2_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${MP4V2_HEADERS}"
)
add_dependencies(mp4v2 mp4v2_external)

install(DIRECTORY ${MP4V2_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*so*"
)

