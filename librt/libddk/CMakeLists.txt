# Project setup
project (ValiDDK)
enable_language (ASM_NASM)
enable_language (C)

set (CMAKE_ASM_NASM_OBJECT_FORMAT ${NASM_DEFAULT_FORMAT})

set (GENERATED_HEADERS
    ${CMAKE_BINARY_DIR}/svc_session_protocol.h
    ${CMAKE_BINARY_DIR}/svc_session_protocol_client.h
    ${CMAKE_BINARY_DIR}/svc_storage_protocol.h
    ${CMAKE_BINARY_DIR}/svc_storage_protocol_client.h
    ${CMAKE_BINARY_DIR}/svc_file_protocol.h
    ${CMAKE_BINARY_DIR}/svc_file_protocol_client.h
    ${CMAKE_BINARY_DIR}/svc_path_protocol.h
    ${CMAKE_BINARY_DIR}/svc_path_protocol_client.h
    ${CMAKE_BINARY_DIR}/svc_process_protocol.h
    ${CMAKE_BINARY_DIR}/svc_process_protocol_client.h
    ${CMAKE_BINARY_DIR}/svc_library_protocol.h
    ${CMAKE_BINARY_DIR}/svc_library_protocol_client.h
    ${CMAKE_BINARY_DIR}/svc_socket_protocol.h
    ${CMAKE_BINARY_DIR}/svc_socket_protocol_client.h
    ${CMAKE_BINARY_DIR}/svc_device_protocol.h
    ${CMAKE_BINARY_DIR}/svc_device_protocol_client.h
    ${CMAKE_BINARY_DIR}/svc_usb_protocol.h
    ${CMAKE_BINARY_DIR}/svc_usb_protocol_client.h

    ${CMAKE_BINARY_DIR}/ctt_driver_protocol.h
    ${CMAKE_BINARY_DIR}/ctt_driver_protocol_client.h
    ${CMAKE_BINARY_DIR}/ctt_storage_protocol.h
    ${CMAKE_BINARY_DIR}/ctt_storage_protocol_client.h
    ${CMAKE_BINARY_DIR}/ctt_usbhub_protocol.h
    ${CMAKE_BINARY_DIR}/ctt_usbhub_protocol_client.h
    ${CMAKE_BINARY_DIR}/ctt_usbhost_protocol.h
    ${CMAKE_BINARY_DIR}/ctt_usbhost_protocol_client.h

    ${CMAKE_BINARY_DIR}/hid_events_protocol.h
)

set (GENERATED_SOURCES
    ${CMAKE_BINARY_DIR}/svc_session_protocol_client.c
    ${CMAKE_BINARY_DIR}/svc_storage_protocol_client.c
    ${CMAKE_BINARY_DIR}/svc_file_protocol_client.c
    ${CMAKE_BINARY_DIR}/svc_path_protocol_client.c
    ${CMAKE_BINARY_DIR}/svc_process_protocol_client.c
    ${CMAKE_BINARY_DIR}/svc_library_protocol_client.c
    ${CMAKE_BINARY_DIR}/svc_socket_protocol_client.c
    ${CMAKE_BINARY_DIR}/svc_device_protocol_client.c
    ${CMAKE_BINARY_DIR}/svc_usb_protocol_client.c
    ${CMAKE_BINARY_DIR}/ctt_driver_protocol_client.c
    ${CMAKE_BINARY_DIR}/ctt_storage_protocol_client.c
    ${CMAKE_BINARY_DIR}/ctt_usbhub_protocol_client.c
    ${CMAKE_BINARY_DIR}/ctt_usbhost_protocol_client.c
)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
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

add_custom_command(
    OUTPUT ${GENERATED_SOURCES} ${GENERATED_HEADERS}
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/service_protocols.xml --out ${CMAKE_BINARY_DIR} --lang-c --client
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/contract_protocols.xml --out ${CMAKE_BINARY_DIR} --lang-c --client
    COMMAND python ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --protocol ${CMAKE_SOURCE_DIR}/protocols/hid_protocol.xml --out ${CMAKE_BINARY_DIR} --lang-c
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/service_protocols.xml ${CMAKE_SOURCE_DIR}/protocols/contract_protocols.xml
)

add_library(libddk
    ${GENERATED_SOURCES}
    ${ASM_SOURCES}
    
    acsupport.c
    bufferpool.c
    bytepool.c
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
install(TARGETS libddk
    ARCHIVE DESTINATION vali-ddk/lib
    LIBRARY DESTINATION vali-ddk/lib
    RUNTIME DESTINATION vali-ddk/bin
)
install(DIRECTORY include/ DESTINATION vali-ddk/include)
install(DIRECTORY ../../protocols/ DESTINATION vali-ddk/share/vali/protocols)
