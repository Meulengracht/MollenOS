# Configure source files
add_kernel_library (vali-core-devicetree ""
    full.c
    tree.c
    resources.c
)
target_link_libraries(vali-core-devicetree PRIVATE vali-fdt)
