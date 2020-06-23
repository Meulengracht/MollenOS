if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_AHCI)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../librt/libgracht/include
    ../../../librt/libusb/include
    ../../../librt/libddk/include
    ../../../librt/libds/include
    ../../../librt/libc/include
    ../../../librt/include
    ../sata
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_protocol_server.c ${CMAKE_CURRENT_BINARY_DIR}/ctt_storage_protocol_server.c
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/contract_protocols.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --include driver,storage --server
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/contract_protocols.xml
)

set (ADDITONAL_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_protocol_server.c
    ${CMAKE_CURRENT_BINARY_DIR}/ctt_storage_protocol_server.c)

add_module_target(ahci ""
    ${ADDITONAL_SOURCES}

    ahci.c
    dispatch.c
    main.c
    manager.c
    port.c
    transactions.c
)
