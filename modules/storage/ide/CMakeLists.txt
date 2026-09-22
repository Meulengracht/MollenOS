if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_IDE)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../librt/libgracht/include
    ../../../librt/libddk/include
    ../../../librt/libds/include
    ../../../librt/libc/include
    ../sata
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_storage_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_module_target(ide ""
    ${ADDITONAL_SOURCES}

    ide.c
    main.c
)
add_dependencies(ide contract_servers)

# IDE is rarely used, and we just have it available for development purposes
# so do not deploy it to initrd to save space
deploy_file_to_initrd(ide "modules" ide.yaml)
