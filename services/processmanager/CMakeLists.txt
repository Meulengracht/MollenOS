enable_language(C)

# Configure the base address for phoenix. this is to simplify things for the loader
# as the loader will not have to relocate.
set (PHOENIX_BASE_ADDRESS "0x100000")
if ("${VALI_ARCH}" STREQUAL "i386")
    set (PHOENIX_BASE_ADDRESS "0x20000000")
elseif ("${VALI_ARCH}" STREQUAL "amd64")
    set (PHOENIX_BASE_ADDRESS "0x8000000000")
else ()
    message(FATAL "phoenix: unsupported architecture ${VALI_ARCH}")
endif ()

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../librt/libgracht/include
    ../../librt/libusb/include
    ../../librt/libddk/include
    ../../librt/libds/include
    ../../librt/libc/include
)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_library_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_process_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_executable (phoenix
        ${ADDITONAL_SOURCES}

        pe/glue.c
        pe/load.c
        pe/utilities.c
        pe/verify.c

        debugger.c
        main.c
        map_parser.c
        process.c
        requests.c
        symbol_loader.c
)

set_target_properties (phoenix
        PROPERTIES
            COMPILE_OPTIONS "-nostdlib;-Wno-address-of-packed-member"
            COMPILE_DEFINITIONS "__SERVICE_IMPL"
            LINK_FLAGS "-Xlinker -entry:__phoenix_main -Xlinker -base:\"${PHOENIX_BASE_ADDRESS}\""
            OUTPUT_NAME "phoenix"
            SUFFIX ".mos"
)
target_link_libraries (phoenix libc libm libds libddk librt gracht_static)
add_dependencies(phoenix service_servers)
