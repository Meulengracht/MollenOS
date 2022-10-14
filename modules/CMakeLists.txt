macro (add_filesystem_target)
    if (${ARGC} GREATER_EQUAL 2)
        set (SERVICE_SOURCES "${ARGN}")
        list (POP_FRONT SERVICE_SOURCES) # target

        add_library(${ARGV0} SHARED ${SERVICE_SOURCES})
        target_link_options(${ARGV0} PRIVATE "LINKER:-noentry")
        target_link_libraries (${ARGV0} PRIVATE fs-common gracht libds libddk c librt)
        add_custom_command (TARGET ${ARGV0} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${ARGV0}> ${VALI_PATH_DEPLOY_SYSTEM_MODS}
        )
    else ()
        message (FATAL_ERROR "Not enough parameters for add_filesystem_target")
    endif ()
endmacro ()

macro (add_module_target)
    if (${ARGC} GREATER_EQUAL 3)
        set (SERVICE_SOURCES "${ARGN}")
        list (POP_FRONT SERVICE_SOURCES) # target
        list (POP_FRONT SERVICE_SOURCES) # compile flags

        add_library(${ARGV0} SHARED ${SERVICE_SOURCES})
        set_target_properties (${ARGV0}
            PROPERTIES
                COMPILE_FLAGS "-Wno-address-of-packed-member ${ARGV1}"
        )
        target_link_options(${ARGV0} PRIVATE "LINKER:-entry:__CrtModuleEntry")
        target_link_libraries (${ARGV0} PRIVATE gracht libds libddk c librt)
        add_custom_command (TARGET ${ARGV0} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${ARGV0}> ${VALI_PATH_DEPLOY_SYSTEM_MODS}
        )
    else ()
        message (FATAL_ERROR "Not enough parameters for add_module_target")
    endif ()
endmacro ()

# build modules
add_subdirectory(arch)
add_subdirectory(serial)
add_subdirectory(storage)
add_subdirectory(filesystems)
add_subdirectory(input)
