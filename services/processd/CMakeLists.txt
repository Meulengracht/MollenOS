enable_language(C)

# Phoenix runs at this virtual address after the kernel maps its pages. Keep the
# relocation records, as already required on ARM64: the UEFI PE loader first
# copies the image into temporary physical memory and rejects images marked as
# fixed when that memory differs from the linked address. No relocation is
# applied to this temporary copy; the kernel still maps it at the address below.
set (PHOENIX_BASE_ADDRESS "0x100000")
set (PHOENIX_LINKER_FLAGS "-entry:__phoenix_main,")
if (VALI_ARCH STREQUAL "i386")
    set (PHOENIX_BASE_ADDRESS "0x20000000")
elseif (VALI_ARCH STREQUAL "amd64")
    set (PHOENIX_BASE_ADDRESS "0x8000000000")
elseif (VALI_ARCH STREQUAL "aarch64")
    set (PHOENIX_BASE_ADDRESS "0x8000000000")
else ()
    message(FATAL_ERROR "phoenix: unsupported architecture ${VALI_ARCH}")
endif ()

# Include any sub-projects here
add_subdirectory(debugger)
add_subdirectory(pe)
add_subdirectory(services)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/sys_library_service_server.c
    ${CMAKE_BINARY_DIR}/protocols/sys_process_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_executable (phoenix
        ${ADDITONAL_SOURCES}

        main.c
        process.c
        requests.c
)

target_include_directories(phoenix PRIVATE include)
target_compile_options(phoenix PRIVATE -nostdlib -Wno-address-of-packed-member)
target_link_options(phoenix PRIVATE "LINKER:${PHOENIX_LINKER_FLAGS}-base:${PHOENIX_BASE_ADDRESS}")
set_target_properties(phoenix
        PROPERTIES
            OUTPUT_NAME "phoenix"
            SUFFIX ".mos"
)
target_link_libraries(phoenix processd-debugger processd-pelib processd-services libds libddk librt gracht_static libm)
add_dependencies(phoenix service_servers)

# setup kernel for boot folder
install(TARGETS phoenix
    ARCHIVE DESTINATION boot-assets/lib
    LIBRARY DESTINATION boot-assets/lib
    RUNTIME DESTINATION boot/payload
)
