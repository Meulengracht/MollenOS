set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_storage_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_module_target(msd ""
    ${ADDITONAL_SOURCES}
    protocols/bulk.c
    protocols/ufi.c

    functions.c
    main.c
    msd.c
)
add_dependencies(msd contract_servers)
target_include_directories(msd PRIVATE
        ../sata
)
target_link_libraries(msd PRIVATE libusb)
deploy_file_to_initrd(msd "modules" msd.yaml)
