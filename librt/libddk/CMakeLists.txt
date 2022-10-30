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
    ${CMAKE_BINARY_DIR}/protocols/sys_mount_service_client.h
    ${CMAKE_BINARY_DIR}/protocols/sys_mount_service_client.c
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

set(ASM_SOURCES
    arch/${VALI_ARCH}/_io.s
)

# Add support for .S extension
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM_NASM)

add_library(libddk
        ${GENERATED_SOURCES}
        ${ASM_SOURCES}

        initrd/depacks.c
        initrd/filter.c
    
        acsupport.c
        bufferpool.c
        bytepool.c
        crc32.c
        debug.c
        device.c
        deviceio.c
        handle.c
        interrupt.c
        io.c
        mappings.c
        service.c
        usb.c
        utils.c
)
add_dependencies(libddk service_clients contract_clients)
target_include_directories(libddk
        PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_CURRENT_SOURCE_DIR}/../libusb/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include>"
)

# avoid using target_link_libraries for the RT libraries as it could result
# in cyclic dependencies, and most are static libraries and do not actually really
# depend on each other
target_include_directories(libddk PRIVATE
        ../../tools/rd/libvafs/include
        ../libgracht/include
        ../libds/include
        ../libos/include
        ../libc/include
)

install(TARGETS libddk
    EXPORT ddk_targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY ../../protocols/ DESTINATION share/vali/protocols)
