if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiFileManager)
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
    include
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/svc_file_protocol_server.c ${CMAKE_CURRENT_BINARY_DIR}/svc_path_protocol_server.c ${CMAKE_CURRENT_BINARY_DIR}/svc_storage_protocol_server.c
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/service_protocols.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --include file,path,storage --server
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/service_protocols.xml
)

set (ADDITONAL_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/svc_file_protocol_server.c
    ${CMAKE_CURRENT_BINARY_DIR}/svc_path_protocol_server.c
    ${CMAKE_CURRENT_BINARY_DIR}/svc_storage_protocol_server.c)

add_service_target(filemanager "-D__FILEMANAGER_IMPL"
    ${ADDITONAL_SOURCES}
    layouts/detect.c
    layouts/mbr.c
    layouts/gpt.c

    functions.c
    modules.c
    path.c
    storage.c
    main.c
)
