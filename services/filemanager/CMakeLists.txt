if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiFileManager)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
    include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_file_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_path_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_storage_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(filemanager "-D__FILEMANAGER_IMPL"
    ${ADDITONAL_SOURCES}
    layouts/detect.c
    layouts/mbr.c
    layouts/gpt.c

    storage/storage.c
    storage/utils.c

    cache.c
    functions.c
    modules.c
    path.c
    main.c
)
add_dependencies(filemanager service_servers)
