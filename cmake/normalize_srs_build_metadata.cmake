foreach(required_variable IN ITEMS
        SRS_AUTO_HEADERS
        SRS_BUILD_EPOCH
        SRS_BUILD_DATE
        SRS_BUILD_UNAME)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT EXISTS "${SRS_AUTO_HEADERS}" OR IS_DIRECTORY "${SRS_AUTO_HEADERS}")
    message(FATAL_ERROR "SRS generated header is missing: ${SRS_AUTO_HEADERS}")
endif()

file(READ "${SRS_AUTO_HEADERS}" srs_auto_headers)

function(replace_srs_build_define define_name define_value)
    string(REGEX MATCHALL
        "#define ${define_name} \"[^\"]*\""
        matching_defines
        "${srs_auto_headers}")
    list(LENGTH matching_defines matching_define_count)
    if(NOT matching_define_count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one ${define_name} in ${SRS_AUTO_HEADERS}")
    endif()

    list(GET matching_defines 0 matching_define)
    string(REPLACE
        "${matching_define}"
        "#define ${define_name} \"${define_value}\""
        srs_auto_headers
        "${srs_auto_headers}")
    set(srs_auto_headers "${srs_auto_headers}" PARENT_SCOPE)
endfunction()

replace_srs_build_define(SRS_BUILD_TS "${SRS_BUILD_EPOCH}")
replace_srs_build_define(SRS_BUILD_DATE "${SRS_BUILD_DATE}")
replace_srs_build_define(SRS_UNAME "${SRS_BUILD_UNAME}")

file(WRITE "${SRS_AUTO_HEADERS}" "${srs_auto_headers}")
