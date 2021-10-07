if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../cmake/SetupEnvironment.cmake)
    project(ValiModules)
endif ()

macro (add_filesystem_target)
    if (${ARGC} GREATER_EQUAL 2)
        set (SERVICE_SOURCES "${ARGN}")
        list (POP_FRONT SERVICE_SOURCES) # target

        if (DEFINED VALI_PATH_INITRD)
            configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${ARGV0}.mdrv ${VALI_PATH_INITRD}/${ARGV0}.mdrv COPYONLY)
        endif ()

        add_library(${ARGV0} SHARED ${SERVICE_SOURCES})
        set_target_properties (${ARGV0}
            PROPERTIES
                LINK_FLAGS "-Xlinker -noentry"
                
        )
        target_link_libraries (${ARGV0} gracht libds libddk c librt)

        if (DEFINED VALI_PATH_INITRD)
            add_custom_command (TARGET ${ARGV0} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${ARGV0}> ${VALI_PATH_INITRD}
            )
        endif ()
    else ()
        message (FATAL_ERROR "Not enough parameters for add_module_target")
    endif ()
endmacro ()

macro (add_module_target)
    if (${ARGC} GREATER_EQUAL 3)
        set (SERVICE_SOURCES "${ARGN}")
        list (POP_FRONT SERVICE_SOURCES) # target
        list (POP_FRONT SERVICE_SOURCES) # compile flags

        if (DEFINED VALI_PATH_INITRD)
            configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${ARGV0}.mdrv ${VALI_PATH_INITRD}/${ARGV0}.mdrv COPYONLY)
        endif ()

        add_library(${ARGV0} SHARED ${SERVICE_SOURCES})
        set_target_properties (${ARGV0}
            PROPERTIES
                COMPILE_FLAGS "-Wno-address-of-packed-member -D__DRIVER_IMPL ${ARGV1}"
                LINK_FLAGS "-Xlinker -entry:__CrtModuleEntry"
        )
        target_link_libraries (${ARGV0} gracht libds libddk c librt)

        if (DEFINED VALI_PATH_INITRD)
            add_custom_command (TARGET ${ARGV0} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${ARGV0}> ${VALI_PATH_INITRD}
            )
        endif ()
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
