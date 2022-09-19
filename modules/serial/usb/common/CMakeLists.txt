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
target_link_libraries(usb-common PUBLIC gracht libds libddk c librt)
target_include_directories(usb-common PUBLIC
        ${CMAKE_BINARY_DIR}/protocols
        ${CMAKE_CURRENT_BINARY_DIR}
        ../../../../librt/libusb/include
        ../../../../librt/libos/include
)
