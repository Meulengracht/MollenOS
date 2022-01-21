if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_EHCI)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../../librt/libgracht/include
    ../../../../librt/libusb/include
    ../../../../librt/libddk/include
    ../../../../librt/libds/include
    ../../../../librt/libc/include
)

add_module_target(ehci ""
    structures/itd.c
    structures/qh.c
    structures/td.c
    controller.c
    interrupt.c
    port.c
    queue.c
    transfer.c
    transfer_generic.c
    transfer_isoc.c
)
target_link_libraries(ehci usb-common)
deploy_to_initrd(ehci ON)
