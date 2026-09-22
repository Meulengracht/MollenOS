enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
)

set (ADDITONAL_SOURCES
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

    manager.c
    socket.c
    main.c
)
add_dependencies(netd service_servers)
deploy_file_to_initrd(netd "services" netd.yaml)

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
