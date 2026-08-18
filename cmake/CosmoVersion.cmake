function(cosmo_resolve_version source_dir fallback_major fallback_minor fallback_patch fallback_build)
    set(_major "${fallback_major}")
    set(_minor "${fallback_minor}")
    set(_patch "${fallback_patch}")
    set(_build "${fallback_build}")
    set(_source "fallback")

    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${source_dir}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags
                    --match "v[0-9]*.[0-9]*.[0-9]*"
                    --long --abbrev=8 HEAD
            WORKING_DIRECTORY "${source_dir}"
            RESULT_VARIABLE _git_result
            OUTPUT_VARIABLE _git_describe
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(_git_result EQUAL 0 AND
           _git_describe MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g[0-9A-Fa-f]+$")
            set(_tag_major "${CMAKE_MATCH_1}")
            set(_tag_minor "${CMAKE_MATCH_2}")
            set(_tag_patch "${CMAKE_MATCH_3}")
            set(_tag_distance "${CMAKE_MATCH_4}")
            set(_tag_version "${_tag_major}.${_tag_minor}.${_tag_patch}")
            set(_fallback_version
                "${fallback_major}.${fallback_minor}.${fallback_patch}")

            # An older repository tag must not pull a prepared release back
            # below the source fallback. Once that release is tagged, later
            # commits advance its patch component automatically.
            if(NOT _tag_version VERSION_LESS _fallback_version)
                set(_major "${_tag_major}")
                set(_minor "${_tag_minor}")
                math(EXPR _patch "${_tag_patch} + ${_tag_distance}")
                set(_build 0)
                set(_source "git:${_git_describe}")
            endif()
        endif()
    endif()

    set(COSMO_VERSION_MAJOR "${_major}" PARENT_SCOPE)
    set(COSMO_VERSION_MINOR "${_minor}" PARENT_SCOPE)
    set(COSMO_VERSION_PATCH "${_patch}" PARENT_SCOPE)
    set(COSMO_VERSION_BUILD "${_build}" PARENT_SCOPE)
    set(COSMO_VERSION_SOURCE "${_source}" PARENT_SCOPE)
endfunction()
