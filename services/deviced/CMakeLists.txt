enable_language(C)

# Shared header paths
add_library(deviced-headers INTERFACE)
target_include_directories(deviced-headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# include subdirectories
add_subdirectory(bus)
add_subdirectory(core)
add_subdirectory(firmware)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_device_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/ctt_driver_service_client.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(deviced "" ${ADDITONAL_SOURCES} main.c)
target_link_libraries(deviced PRIVATE
    deviced-headers
    deviced-core
    deviced-pci
)
add_dependencies(deviced service_servers contract_clients)
deploy_file_to_initrd(deviced "services" deviced.yaml)
