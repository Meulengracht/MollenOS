macro (add_app_target)
    if (${ARGC} GREATER_EQUAL 3)
        set (SERVICE_SOURCES "${ARGN}")
        list (POP_FRONT SERVICE_SOURCES) # target
        list (POP_FRONT SERVICE_SOURCES) # compile flags
        list (POP_FRONT SERVICE_SOURCES) # compile defines
        string(REPLACE " " ";" APP_DEFINES "${ARGV2}")

        add_executable(${ARGV0} ${SERVICE_SOURCES})
        set_target_properties (${ARGV0}
            PROPERTIES
                COMPILE_FLAGS "${ARGV1}"
                COMPILE_DEFINITIONS "${APP_DEFINES}"
        )
        target_link_libraries (${ARGV0} gracht libds libddk c librt)
        add_custom_command (TARGET ${ARGV0} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${ARGV0}> ${VALI_PATH_DEPLOY_SHARED_BIN}
        )
    else ()
        message (FATAL_ERROR "Not enough parameters for add_test_target")
    endif ()
endmacro ()

string(TIMESTAMP BUILD_DATE "%Y%m%d")
if (NOT VALI_BUILD_TYPE)
    # Get the latest abbreviated commit hash of the working branch
    execute_process(
            COMMAND git log -1 --format=%h
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            OUTPUT_VARIABLE GIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set (VALI_BUILD_TYPE "${GIT_HASH}-dev")
endif ()

# build osstat that provide information about the current os
add_app_target(osstat ""
        "OS_VERSION=\\\"${VALI_VERSION}\\\" OS_BUILD_DATE=\\\"${BUILD_DATE}\\\" OS_BUILD=\\\"${VALI_BUILD_TYPE}\\\" OS_ARCH=\\\"${VALI_ARCH}\\\""
        osstat.c
)
add_app_target(systat "" "" systat.c)
