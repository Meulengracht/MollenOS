# Project setup
project (ValiDDK)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_OBJECT_FORMAT ${NASM_DEFAULT_FORMAT})

set (GENERATED_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_session_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_session_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_storage_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_storage_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_file_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_file_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_path_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_path_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_process_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_process_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_library_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_library_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_socket_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_socket_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_device_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_device_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_usb_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_usb_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_storage_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/ctt_storage_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhub_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhub_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhost_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/ctt_usbhost_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_input_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/ctt_input_service_client.c
)
set_source_files_properties ( ${GENERATED_SOURCES} PROPERTIES GENERATED TRUE )

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libds/include
    ../../librt/libc/include
    ../../librt/include
    ../../include
    ./include
)

set(ASM_SOURCES
    arch/${VALI_ARCH}/_io.s
)

# Add support for .S extension
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)

add_library(libddk
    ${GENERATED_SOURCES}
    ${ASM_SOURCES}
    
    acsupport.c
    bufferpool.c
    bytepool.c
    debug.c
    device.c
    deviceio.c
    eventqueue.c
    handle.c
    interrupt.c
    io.c
    mappings.c
    service.c
    threadpool.c
    usb.c
    utils.c
)
add_dependencies(libddk service_clients contract_clients)
install(TARGETS libddk
    ARCHIVE DESTINATION vali-ddk/lib
    LIBRARY DESTINATION vali-ddk/lib
    RUNTIME DESTINATION vali-ddk/bin
)
install(DIRECTORY include/ DESTINATION vali-ddk/include)
install(DIRECTORY ../../protocols/ DESTINATION vali-ddk/share/vali/protocols)
