# The FDT parser library, the reason for the source level library
# is due to the need of different compilation targets. The kernel, deviced and
# the rpi loader use different compilation flags and settings.
add_library(vali-fdt INTERFACE)

# Add the FDT parser library as an interface library
target_sources(vali-fdt INTERFACE 
    ${CMAKE_CURRENT_LIST_DIR}/parser.c
)

# Allow users of the parser's types to use headers without its source.
add_library(vali-fdt-headers INTERFACE)
target_include_directories(vali-fdt-headers INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
target_link_libraries(vali-fdt INTERFACE vali-fdt-headers)
