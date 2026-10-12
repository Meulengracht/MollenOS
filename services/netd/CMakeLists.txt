enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/ctt_netadapter_service_client.c
    ${CMAKE_BINARY_DIR}/protocols/sys_socket_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(netd ""
    ${ADDITONAL_SOURCES}
    domains/bt.c
    domains/domains.c
    domains/internet.c
    domains/local.c
    domains/unspec.c

    adapters/adapter.c
    adapters/adapters.c
    adapters/buffers.c
    adapters/client.c
    adapters/events.c
    adapters/rx.c
    adapters/tx.c
    adapters/queue.c
    adapters/requests.c
    adapters/scheduler.c

    manager.c
    socket.c
    main.c
)
add_dependencies(netd service_servers contract_clients)
deploy_file_to_initrd(netd "services" netd.yaml)

# Live DMA-pool registration is for trusted development images until Vali has
# authenticated IPC peers, per-peer SHM grants and protected DMA pins.
option(VALI_NET_ADAPTER_TESTS "Boot netadapter fake and IPC regression tests (trusted test images only)" OFF)
option(VALI_VIRTIO_NET_TESTS "Boot real Virtio network acceptance tests" OFF)
if (VALI_VIRTIO_NET_TESTS AND VALI_NET_ADAPTER_TESTS)
    message(FATAL_ERROR "Select either fake or Virtio network tests; they own the same netd hooks")
endif ()

option(VALI_NET_TESTS "Run socket regression tests at boot (QEMU/test images only)" OFF)
if (VALI_NET_TESTS)
    add_service_target(nettest "" ../../testing/net_socket_test.c)
    add_dependencies(nettest service_clients)
    add_custom_target(nettest_deploy ALL
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:nettest>" "${VALI_PATH_DEPLOY_SYSTEM_SRV}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/testing/nettest.yaml" "${VALI_PATH_DEPLOY_SYSTEM_SRV}"
        DEPENDS nettest
        VERBATIM
    )
else ()
    # Do not leave the opt-in service in images after switching tests off.
    file(REMOVE "${VALI_PATH_DEPLOY_SYSTEM_SRV}/nettest.dll"
                "${VALI_PATH_DEPLOY_SYSTEM_SRV}/nettest.yaml")
endif ()

# The fake is a separate process. The test workload runs in netd so it exercises
# the production generated client, worker, session engine and kernel SHM pools.
if (VALI_NET_ADAPTER_TESTS)
    target_compile_definitions(netd PRIVATE VALI_NET_ADAPTER_TESTS)
    target_include_directories(netd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_sources(netd PRIVATE ../../testing/netadapter/ipc_test.c)
    set(FAKE_BINDING ${CMAKE_BINARY_DIR}/protocols/ctt_netadapter_service_server.c)
    set_source_files_properties(${FAKE_BINDING} PROPERTIES GENERATED TRUE)
    add_service_target(netadapter_fake "" ../../testing/netadapter/fake.c ${FAKE_BINDING})
    add_dependencies(netadapter_fake contract_servers)
    add_custom_target(netadapter_fake_deploy ALL
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:netadapter_fake>" "${VALI_PATH_DEPLOY_SYSTEM_SRV}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/testing/netadapter/fake.yaml"
            "${VALI_PATH_DEPLOY_SYSTEM_SRV}/netadapter_fake.yaml"
        DEPENDS netadapter_fake VERBATIM)
else ()
    file(REMOVE "${VALI_PATH_DEPLOY_SYSTEM_SRV}/netadapter_fake.dll"
                "${VALI_PATH_DEPLOY_SYSTEM_SRV}/netadapter_fake.yaml")
endif ()

if (VALI_VIRTIO_NET_TESTS)
    target_compile_definitions(netd PRIVATE VALI_VIRTIO_NET_TESTS)
    target_include_directories(netd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_sources(netd PRIVATE ../../testing/netadapter/virtio_ipc_test.c)
endif ()
