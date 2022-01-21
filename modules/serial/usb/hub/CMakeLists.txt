if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_USBHUB)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../../librt/libgracht/include
    ../../../../librt/libusb/include
    ../../../../librt/libddk/include
    ../../../../librt/libds/include
    ../../../../librt/libc/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhub_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_module_target(usbhub ""
    ${ADDITONAL_SOURCES}
    functions.c
    hub.c
    main.c
)
add_dependencies(usbhub contract_servers)
target_link_libraries(usbhub libusb)
deploy_to_initrd(usbhub ON)
