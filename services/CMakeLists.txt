if (VALI_BUILD_UNIT_TESTS)
    # add all targets that support unit testing
    add_subdirectory (filed)
    return ()
endif ()


macro (add_service_target)
    if (${ARGC} GREATER_EQUAL 3)
        set (SERVICE_SOURCES "${ARGN}")
        list (POP_FRONT SERVICE_SOURCES) # target
        list (POP_FRONT SERVICE_SOURCES) # compile flags

        add_library(${ARGV0} SHARED ${SERVICE_SOURCES})
        set_target_properties (${ARGV0}
            PROPERTIES
                COMPILE_FLAGS "-Wno-address-of-packed-member -D__SERVICE_IMPL ${ARGV1}"
        )
        target_link_options(${ARGV0} PRIVATE "LINKER:-entry:__CrtServiceEntry")
        target_link_libraries (${ARGV0} PRIVATE gracht libusb libds libddk c librt)
        add_custom_command (TARGET ${ARGV0} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${ARGV0}> ${VALI_PATH_DEPLOY_SYSTEM_SRV}
        )
    else ()
        message (FATAL_ERROR "Not enough parameters for add_service_target")
    endif ()
endmacro ()

# build services
add_subdirectory(deviced)
add_subdirectory(filed)
add_subdirectory(processd)
add_subdirectory(served)
add_subdirectory(sessiond)
add_subdirectory(usbd)
add_subdirectory(netd)

