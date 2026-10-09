if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_VirtioNet)
endif ()

enable_language(C)

include_directories(
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../librt/libgracht/include
    ../../../librt/libddk/include
    ../../../librt/libds/include
    ../../../librt/libc/include
)

set(GENERATED_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_netadapter_service_server.c
)
set_source_files_properties(${GENERATED_SOURCES} PROPERTIES GENERATED TRUE)

add_module_target(virtio-net "" 
    ${GENERATED_SOURCES}
    main.c
    device.c
    interrupt.c
    queue.c
    pools.c
    session.c
    requests.c
)
target_link_libraries(virtio-net PRIVATE virtio-common)
add_dependencies(virtio-net contract_servers)
deploy_file_to_initrd(virtio-net "modules" virtio-net.yaml)
