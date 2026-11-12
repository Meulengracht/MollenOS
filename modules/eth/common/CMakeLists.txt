# vali common ethernet framework library

set(GENERATED_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_netadapter_service_server.c
)
set_source_files_properties(${GENERATED_SOURCES} PROPERTIES GENERATED TRUE)

add_library(eth-common STATIC
    ${GENERATED_SOURCES}
    mdio.c
    phy.c
    phy-broadcom.c
    dma.c
    netadapter.c
    pools.c
    session.c
    requests.c
    events.c
)
add_dependencies(eth-common contract_servers)
target_link_libraries(eth-common PUBLIC gracht libds libddk c librt)
target_include_directories(eth-common PUBLIC ${CMAKE_BINARY_DIR}/protocols ${CMAKE_CURRENT_SOURCE_DIR})

option(VALI_ETHERNET_DIAGNOSTICS "Log Ethernet initialization and packet diagnostics" OFF)
if(VALI_ETHERNET_DIAGNOSTICS)
    target_compile_definitions(eth-common PUBLIC VALI_ETHERNET_DIAGNOSTICS=1)
endif()
