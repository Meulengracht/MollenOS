if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_USBCOMMON)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../../librt/libgracht/include
    ../../../../librt/libusb/include
    ../../../../librt/libddk/include
    ../../../../librt/libds/include
    ../../../../librt/libc/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhost_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhub_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_library(usb-common STATIC
    ${ADDITONAL_SOURCES}

    manager.c
    scheduler.c
    scheduler_chain.c
    scheduler_periodic.c
    scheduler_settings.c
    service.c
    transfer.c
)
add_dependencies(usb-common contract_servers)
