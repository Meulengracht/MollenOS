add_module_target(uhci ""
    structures/qh.c
    structures/td.c
    controller.c
    interrupts.c
    io.c
    port.c
    queue.c
    transfer.c
    transfer_generic.c
)
target_link_libraries(uhci PRIVATE usb-common)
deploy_file_to_initrd(uhci "modules" uhci.yaml)
