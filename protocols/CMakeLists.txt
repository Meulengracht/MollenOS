set(SERVICES  "device;file;mount;net/sys/socket;process;served/chef/served;session;storage;syslog/sys/log;usb")
set(CONTRACTS "driver;filesystem;input;storage;usbhost;usbhub")
set(GENERATOR_PATH "${CMAKE_SOURCE_DIR}/librt/libgracht/generator/parser.py")

# Generate all the service files
foreach(SVC ${SERVICES})
    set(SVC_FILENAME ${SVC})
    set(SVC_NAMESPACE "sys")
    set(SVC_NAME ${SVC})
    string(FIND ${SVC} "/" SVC_SPLIT_LOC)
    if (NOT ${SVC_SPLIT_LOC} EQUAL -1)
        string(REPLACE "/" ";" SVC_TOKENS ${SVC})
        list(GET SVC_TOKENS 0 SVC_FILENAME)
        list(GET SVC_TOKENS 1 SVC_NAMESPACE)
        list(GET SVC_TOKENS 2 SVC_NAME)
    endif()

    set(SVC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${SVC_NAMESPACE}_${SVC_NAME}_service.h")
    set(SVC_CLIENT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${SVC_NAMESPACE}_${SVC_NAME}_service_client.h")
    set(SVC_CLIENT_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${SVC_NAMESPACE}_${SVC_NAME}_service_client.c")
    set(SVC_SERVER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${SVC_NAMESPACE}_${SVC_NAME}_service_server.h")
    set(SVC_SERVER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${SVC_NAMESPACE}_${SVC_NAME}_service_server.c")
    add_custom_command(
            OUTPUT ${SVC_HEADER} ${SVC_CLIENT_HEADER} ${SVC_CLIENT_SOURCE} ${SVC_SERVER_HEADER} ${SVC_SERVER_SOURCE}
            COMMAND python3 ${GENERATOR_PATH} --service ${CMAKE_CURRENT_SOURCE_DIR}/${SVC_FILENAME}.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SVC_FILENAME}.gr
    )
    set(GEN_SERVICE_HEADERS ${GEN_SERVICE_HEADERS} ${SVC_HEADER})
    set(GEN_SERVICE_CLIENT_HEADERS ${GEN_SERVICE_CLIENT_HEADERS} ${SVC_CLIENT_HEADER})
    set(GEN_SERVICE_CLIENT_SOURCES ${GEN_SERVICE_CLIENT_SOURCES} ${SVC_CLIENT_SOURCE})
    set(GEN_SERVICE_SERVER_HEADERS ${GEN_SERVICE_SERVER_HEADERS} ${SVC_SERVER_HEADER})
    set(GEN_SERVICE_SERVER_SOURCES ${GEN_SERVICE_SERVER_SOURCES} ${SVC_SERVER_SOURCE})
endforeach()

# Generate all the contracts files
foreach(CTS ${CONTRACTS})
    set(CTS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ctt_${CTS}_service.h")
    set(CTS_CLIENT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ctt_${CTS}_service_client.h")
    set(CTS_CLIENT_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/ctt_${CTS}_service_client.c")
    set(CTS_SERVER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ctt_${CTS}_service_server.h")
    set(CTS_SERVER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/ctt_${CTS}_service_server.c")
    add_custom_command(
            OUTPUT ${CTS_HEADER} ${CTS_CLIENT_HEADER} ${CTS_CLIENT_SOURCE} ${CTS_SERVER_HEADER} ${CTS_SERVER_SOURCE}
            COMMAND python3 ${GENERATOR_PATH} --service ${CMAKE_CURRENT_SOURCE_DIR}/contracts/${CTS}.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --client --server
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/contracts/${CTS}.gr
    )
    set(GEN_CONTRACT_HEADERS ${GEN_CONTRACT_HEADERS} ${CTS_HEADER})
    set(GEN_CONTRACT_CLIENT_HEADERS ${GEN_CONTRACT_CLIENT_HEADERS} ${CTS_CLIENT_HEADER})
    set(GEN_CONTRACT_CLIENT_SOURCES ${GEN_CONTRACT_CLIENT_SOURCES} ${CTS_CLIENT_SOURCE})
    set(GEN_CONTRACT_SERVER_HEADERS ${GEN_CONTRACT_SERVER_HEADERS} ${CTS_SERVER_HEADER})
    set(GEN_CONTRACT_SERVER_SOURCES ${GEN_CONTRACT_SERVER_SOURCES} ${CTS_SERVER_SOURCE})
endforeach()

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
