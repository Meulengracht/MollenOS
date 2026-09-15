# Compile definitions are a CMake list; functions preserve argument backslashes.
function (add_app_target target compile_flags compile_definitions)
    add_executable(${target} ${ARGN})
    set_target_properties (${target} PROPERTIES COMPILE_FLAGS "${compile_flags}")
    target_compile_definitions (${target} PRIVATE ${compile_definitions})
    target_link_libraries (${target} gracht libds libddk c librt)
    add_custom_command (TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${VALI_PATH_DEPLOY_SHARED_BIN}"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${target}>" "${VALI_PATH_DEPLOY_SHARED_BIN}"
        VERBATIM
    )
endfunction ()

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
        "OS_VERSION=\"${VALI_VERSION}\";OS_BUILD_DATE=\"${BUILD_DATE}\";OS_BUILD=\"${VALI_BUILD_TYPE}\";OS_ARCH=\"${VALI_ARCH}\""
        osstat.c
)
add_app_target(systat "" "" systat.c)
