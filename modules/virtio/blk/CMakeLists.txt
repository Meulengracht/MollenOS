if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_VirtioBlk)
endif ()

enable_language(C)

include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../librt/libgracht/include
    ../../../librt/libddk/include
    ../../../librt/libds/include
    ../../../librt/libc/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_storage_service_server.c
)
set_source_files_properties (${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE)

add_module_target(virtio-blk ""
    ${ADDITONAL_SOURCES}

    device.c
    main.c
)
target_link_libraries(virtio-blk PRIVATE virtio-common)
add_dependencies(virtio-blk contract_servers)
deploy_file_to_initrd(virtio-blk "modules" virtio-blk.yaml)