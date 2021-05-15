if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_MSD)
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
    ../../../librt/include
    ../sata
)

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
target_link_libraries(msd libusb)
