add_module_target(ohci ""
    structures/itd.c
    structures/qh.c
    structures/td.c
    controller.c
    interrupts.c
    port.c
    queue.c
    transfer.c
    transfer_generic.c
    transfer_isoc.c
)
target_link_libraries(ohci PRIVATE usb-common)
deploy_file_to_initrd(ohci "modules" ohci.yaml)
