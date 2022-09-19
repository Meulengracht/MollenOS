set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhub_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_module_target(usbhub ""
    ${ADDITONAL_SOURCES}
    functions.c
    hub.c
    main.c
)
add_dependencies(usbhub contract_servers)
target_link_libraries(usbhub PRIVATE libusb)
deploy_file_to_initrd(usbhub "modules" usbhub.yaml)
