if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiUsbManager)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
    ../../librt/include
)

set (ADDITONAL_SOURCES
        ${CMAKE_BINARY_DIR}/protocols/svc_usb_protocol_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(usbmanager ""
        ${ADDITONAL_SOURCES}

        port_event.c
        controllers.c
        hubs.c
        devices.c
        main.c
)
add_dependencies(usbmanager service_servers)
