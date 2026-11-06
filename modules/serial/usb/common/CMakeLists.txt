set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhost_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhub_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_library(usb-common STATIC
    ${ADDITONAL_SOURCES}

    manager.c
    service.c
    scheduler/scheduler.c
    scheduler/arena.c
    scheduler/chain.c
    scheduler/periodic.c
    scheduler/settings.c
    transfer/transfer.c
    transfer/dma.c
    transfer/bounce.c
)
add_dependencies(usb-common contract_servers)
target_link_libraries(usb-common PUBLIC gracht libds libddk c librt)
target_include_directories(usb-common PUBLIC
        ${CMAKE_BINARY_DIR}/protocols
        ${CMAKE_CURRENT_BINARY_DIR}
        ../../../../librt/libusb/include
        ../../../../librt/libos/include
)
