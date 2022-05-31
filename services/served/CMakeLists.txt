if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiSessionManager)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_session_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(served ""
    ${ADDITONAL_SOURCES}
    
    main.c
)
add_dependencies(served service_servers)
deploy_file_to_initrd(served "services" served.yaml)

