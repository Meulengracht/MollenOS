
set (GEN_SERVICE_HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/sys_session_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_storage_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_file_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_path_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_process_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_library_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_socket_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_device_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_usb_service.h
)

set (GEN_SERVICE_CLIENT_HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/sys_session_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_storage_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_file_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_path_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_process_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_library_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_socket_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_device_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_usb_service_client.h
)

set (GEN_SERVICE_SERVER_HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/sys_session_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_storage_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_file_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_path_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_process_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_library_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_socket_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_device_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/sys_usb_service_server.h
)

set (GEN_SERVICE_CLIENT_SOURCES
        sys_session_service_client.c
        sys_storage_service_client.c
        sys_file_service_client.c
        sys_path_service_client.c
        sys_process_service_client.c
        sys_library_service_client.c
        sys_socket_service_client.c
        sys_device_service_client.c
        sys_usb_service_client.c
)

set (GEN_SERVICE_SERVER_SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/sys_session_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/sys_storage_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/sys_file_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/sys_path_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/sys_process_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/sys_library_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/sys_socket_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/sys_device_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/sys_usb_service_server.c
)

set (GEN_CONTRACT_HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_storage_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_service.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_input_service.h
)

set (GEN_CONTRACT_CLIENT_HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_storage_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_service_client.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_input_service_client.h
)

set (GEN_CONTRACT_SERVER_HEADERS
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_storage_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_service_server.h
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_input_service_server.h
)

set (GEN_CONTRACT_CLIENT_SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_service_client.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_storage_service_client.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_service_client.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_service_client.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_input_service_client.c
)

set (GEN_CONTRACT_SERVER_SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_driver_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_storage_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhub_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_usbhost_service_server.c
        ${CMAKE_CURRENT_BINARY_DIR}/ctt_input_service_server.c
)

add_custom_command(
        OUTPUT
            ${GEN_SERVICE_HEADERS}
            ${GEN_SERVICE_CLIENT_HEADERS}
            ${GEN_SERVICE_CLIENT_SOURCES}
            ${GEN_SERVICE_SERVER_HEADERS}
            ${GEN_SERVICE_SERVER_SOURCES}
            ${GEN_CONTRACT_HEADERS}
            ${GEN_CONTRACT_CLIENT_HEADERS}
            ${GEN_CONTRACT_CLIENT_SOURCES}
            ${GEN_CONTRACT_SERVER_HEADERS}
            ${GEN_CONTRACT_SERVER_SOURCES}
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/session.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/file.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/process.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/device.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/net.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/usb.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/contracts/driver.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/contracts/storage.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/contracts/usbhost.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/contracts/usbhub.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        COMMAND python3 ${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/contracts/input.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/session.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/file.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/process.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/device.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/net.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/usb.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/contracts/driver.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/contracts/storage.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/contracts/usbhost.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/contracts/usbhub.gr
            ${CMAKE_CURRENT_SOURCE_DIR}/contracts/input.gr
)

add_custom_target(
        service_clients
        DEPENDS ${GEN_SERVICE_CLIENT_HEADERS} ${GEN_SERVICE_CLIENT_SOURCES}
)

add_custom_target(
        service_servers
        DEPENDS ${GEN_SERVICE_SERVER_HEADERS} ${GEN_SERVICE_SERVER_SOURCES}
)

add_custom_target(
        contract_clients
        DEPENDS ${GEN_CONTRACT_CLIENT_HEADERS} ${GEN_CONTRACT_CLIENT_SOURCES}
)

add_custom_target(
        contract_servers
        DEPENDS ${GEN_CONTRACT_SERVER_HEADERS} ${GEN_CONTRACT_SERVER_SOURCES}
)
