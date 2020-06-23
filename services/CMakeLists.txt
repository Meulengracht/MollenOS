if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../cmake/SetupEnvironment.cmake)
    project(ValiServices)
endif ()

macro (add_service_target)
    if (${ARGC} GREATER_EQUAL 3)
        set (SERVICE_SOURCES "${ARGN}")
        list (POP_FRONT SERVICE_SOURCES) # target
        list (POP_FRONT SERVICE_SOURCES) # compile flags

        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${ARGV0}.mdrv ${CMAKE_BINARY_DIR}/${ARGV0}.mdrv COPYONLY)

        add_library(${ARGV0} SHARED ${SERVICE_SOURCES})
        set_target_properties (${ARGV0}
            PROPERTIES
                COMPILE_FLAGS "-Wno-address-of-packed-member -D__SERVICE_IMPL ${ARGV1}"
                LINK_FLAGS "/lldmap /entry:__CrtServiceEntry"
        )
        target_link_libraries (${ARGV0} libgracht libusb libds libddk c librt)
    else ()
        message (FATAL_ERROR "Not enough parameters for add_service_target")
    endif ()
endmacro ()

# build services
add_subdirectory(devicemanager)
add_subdirectory(filemanager)
add_subdirectory(processmanager)
add_subdirectory(sessionmanager)
add_subdirectory(usbmanager)
