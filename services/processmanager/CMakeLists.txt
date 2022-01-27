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

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_library_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_process_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_executable (phoenix
        ${ADDITONAL_SOURCES}

        bootstrap/discover.c

        pe/glue.c
        pe/load.c
        pe/utilities.c
        pe/verify.c

        debugger/debugger.c
        debugger/map_parser.c
        debugger/symbol_loader.c

        main.c
        process.c
        requests.c
)

target_include_directories(phoenix PRIVATE include)
target_compile_options(phoenix PRIVATE -nostdlib -Wno-address-of-packed-member)
target_compile_definitions(phoenix PRIVATE -D__SERVICE_IMPL)
target_link_options(phoenix PRIVATE "LINKER:-entry:__phoenix_main,-fixed-base:\"${PHOENIX_BASE_ADDRESS}\"")
set_target_properties (phoenix
        PROPERTIES
            OUTPUT_NAME "phoenix"
            SUFFIX ".mos"
)
target_link_libraries (phoenix libds libddk librt gracht_static vafs libc libm)
add_dependencies(phoenix service_servers)
