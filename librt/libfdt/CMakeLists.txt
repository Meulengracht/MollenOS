# The FDT parser library, the reason for the source level library
# is due to the need of different compilation targets. The kernel, deviced and
# the rpi loader use different compilation flags and settings.
add_library(vali-fdt INTERFACE)

# Add the FDT parser library as an interface library
target_sources(vali-fdt INTERFACE 
    ${CMAKE_CURRENT_LIST_DIR}/parser.c
)

# Setup required includes
target_include_directories(vali-fdt INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
