if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_HID)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../librt/libgracht/include
    ../../../librt/libusb/include
    ../../../librt/libddk/include
    ../../../librt/libds/include
    ../../../librt/libc/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_input_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

set (MODULE_HEADERS
        hid.h
        hid_keycodes.h
)

add_module_target(hid ""
        ${MODULE_HEADERS}
        ${ADDITONAL_SOURCES}
        report_parser.c
        report_handler.c
        functions.c
        hid.c
        main.c
)
add_dependencies(hid contract_servers)
target_link_libraries(hid libusb)
deploy_file_to_initrd(hid "modules" hid.yaml)
