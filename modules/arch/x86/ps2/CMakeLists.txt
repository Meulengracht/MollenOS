if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_PS2)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../../librt/libgracht/include
    ../../../../librt/libusb/include
    ../../../../librt/libddk/include
    ../../../../librt/libds/include
    ../../../../librt/libc/include
    ../../../../librt/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_protocol_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_input_protocol_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_module_target(ps2 ""
    ${ADDITONAL_SOURCES}
    devices/scancodes/scancodes2.c
    devices/keyboard.c
    devices/mouse.c

    main.c
    port.c
)
add_dependencies(ps2 contract_servers)
