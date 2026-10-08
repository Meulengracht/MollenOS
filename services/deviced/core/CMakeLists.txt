add_library(deviced-core STATIC
    configparser.c
    discover.c
    match.c
    initrd.c
    devices.c
    publication.c
    requests.c
)

target_link_libraries(deviced-core PRIVATE
    deviced-headers
    gracht
    libddk
    libds
    libos-api
    c
    libyaml
    vafs
)
add_dependencies(deviced-core service_servers contract_clients)
